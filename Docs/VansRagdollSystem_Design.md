# VansRagdollSystem 布娃娃物理系统 — 完整方案文档

## 1. 背景与目标

VansForestEngine 已具备：
- `VansAnimationController`：基于状态机的骨骼动画驱动，每帧输出骨骼全局变换矩阵缓存（`m_CachedGlobalTransforms`）和 GPU SSBO（`m_BoneMatricesSSBO`）
- `VansPhysicsSystem`（PhysX 5）：刚体、CCT、布料、碰撞事件
- `VansBoneAttachmentSystem`：已有将物理节点附着到骨骼的基础框架

**目标**：在不修改动画状态机的前提下，以最小侵入实现角色布娃娃效果，支持以下三种驱动模式：

| 模式 | 行为 |
|------|------|
| `Animation`（默认） | PhysX 体跟随动画（Kinematic），骨骼矩阵不变 |
| `Physics` | PhysX 体自由模拟，将物理姿态写回骨骼矩阵 |
| `Blend` | 按权重混合动画姿态与物理姿态后写回 |

---

## 2. 架构总览

```
┌──────────────────────────────────────────────────────────┐
│                    VansScene::UpdateAnimations            │
│                                                          │
│  animNode->Update(dt)                                    │
│      └─ VansAnimationController::Update()                │
│             ├─ 状态机推进 / clip 采样 / 混合              │
│             └─ m_CachedGlobalTransforms (模型空间骨骼矩阵)│
│                                                          │
│  VansRagdollSystem::PostAnimationUpdate(animNode)  ◄─NEW─┤
│      ├─ Animation: setKinematicTarget                    │
│      ├─ Physics:   体位姿 → FeedExternalBoneWorldTransforms│
│      └─ Blend:     lerp/slerp → FeedExternalBoneWorldTransforms│
│                                                          │
│  animNode->UploadBoneMatrices(0)                         │
│      └─ 将 SSBO 上传 GPU                                 │
└──────────────────────────────────────────────────────────┘
```

---

## 3. 数据结构

### 3.1 VansRagdollTypes.h

#### RagdollDriveMode

```cpp
enum class RagdollDriveMode
{
    Animation,   // PhysX 体 Kinematic，跟随动画
    Physics,     // 骨骼矩阵由 PhysX 体姿态驱动
    Blend        // 按 blendWeight 混合
};
```

#### RagdollBodyConfig（每骨体配置，可序列化）

```cpp
struct RagdollBodyConfig
{
    std::string boneName;           // 关联骨骼名

    // 碰撞形状："capsule" | "box" | "sphere"
    std::string shapeType = "capsule";
    float capsuleRadius     = 0.08f;
    float capsuleHalfHeight = 0.15f;
    glm::vec3 boxExtents    = {0.1f, 0.2f, 0.1f};
    float sphereRadius      = 0.1f;

    // 物理参数
    float mass             = 5.0f;
    float staticFriction   = 0.5f;
    float dynamicFriction  = 0.4f;
    float restitution      = 0.1f;

    // 骨骼局部空间下的形状偏移（Euler ZYX，单位度）
    glm::vec3 offsetPosition = {0, 0, 0};
    glm::vec3 offsetRotation = {0, 0, 0};

    std::string layerName = "Default"; // 碰撞层
};
```

#### RagdollJointConfig（每关节 D6 约束配置，可序列化）

```cpp
struct RagdollJointConfig
{
    std::string childBoneName;     // 子骨骼名（父骨骼从 Skeleton 层级自动查找）

    // 摆动限制（度，Y/Z 轴）
    float swingYLimit = 45.0f;
    float swingZLimit = 45.0f;

    // 扭转限制（度，X 轴）
    float twistLowLimit  = -30.0f;
    float twistHighLimit =  30.0f;

    // 角度限制弹簧（软限制）
    float limitStiffness = 20.0f;
    float limitDamping   = 2.0f;

    // 投影容差（米）
    float projectionTolerance = 0.1f;

    // 可选：D6 Drive（Blend 模式下吸引物理体归位）
    bool  enableDrive     = false;
    float driveStiffness  = 0.0f;
    float driveDamping    = 20.0f;
    float driveForceLimit = PX_MAX_F32;
};
```

#### RagdollProfile（资产文件，.vragdoll）

```cpp
struct RagdollProfile
{
    std::string name;
    std::vector<RagdollBodyConfig>  bodies;
    std::vector<RagdollJointConfig> joints;

    static bool LoadFromFile(const std::string& filePath, RagdollProfile& out);
    static bool LoadFromJson(const nlohmann::json& j, RagdollProfile& out);
};
```

#### RagdollBoneEntry / RagdollInstance（运行时）

```cpp
struct RagdollBoneEntry
{
    std::string      boneName;
    int              boneIndex = -1;
    PxRigidDynamic*  body      = nullptr;
    PxD6Joint*       joint     = nullptr;  // 连接到父体的关节，根体为 nullptr
    PxMaterial*      material  = nullptr;

    // 骨骼 → 体 的局部偏移（在 CreateRagdoll 时计算一次）
    glm::mat4 shapeOffset        = glm::mat4(1.0f);
    glm::mat4 shapeOffsetInverse = glm::mat4(1.0f);
};

struct RagdollInstance
{
    VansAnimationNode*  animNode   = nullptr;
    RagdollDriveMode    driveMode  = RagdollDriveMode::Animation;
    float               blendWeight = 0.0f; // 0=纯动画, 1=纯物理

    std::vector<RagdollBoneEntry>          boneEntries;
    std::unordered_map<std::string, int>   boneNameToEntryIndex;
};
```

---

## 4. VansRagdollSystem 接口

```cpp
class VansRagdollSystem
{
public:
    static VansRagdollSystem& GetInstance();

    void Initialize();
    void Shutdown();   // 释放所有实例，在 UnLoadScene 调用

    // ── 生命周期 ──────────────────────────────────────────────
    // 创建骨骼体和 D6 关节；需要在 animNode->Update() 至少调用一次后才能调用（需要 bind pose）
    bool CreateRagdoll(VansAnimationNode* animNode, const RagdollProfile& profile);
    void DestroyRagdoll(VansAnimationNode* animNode);
    bool HasRagdoll(VansAnimationNode* animNode) const;

    // ── 驱动模式 ───────────────────────────────────────────────
    void             SetDriveMode(VansAnimationNode* animNode, RagdollDriveMode mode);
    RagdollDriveMode GetDriveMode(VansAnimationNode* animNode) const;

    // blendWeight: 0.0=全动画，1.0=全物理（Blend 模式专用）
    void  SetBlendWeight(VansAnimationNode* animNode, float weight);
    float GetBlendWeight(VansAnimationNode* animNode) const;

    // ── 物理冲量 ───────────────────────────────────────────────
    void ApplyImpulse(VansAnimationNode* animNode,
                      const std::string& boneName,
                      const glm::vec3& worldImpulse);

    // ── 每帧钩子（在 animNode->Update 之后、UploadBoneMatrices 之前调用）
    void PostAnimationUpdate(VansAnimationNode* animNode);

private:
    // Animation 模式：setKinematicTarget → PhysX 体跟随动画
    void SyncBodiesToAnimPose(RagdollInstance& inst);

    // Physics 模式：体姿态 → 模型空间骨骼变换 → FeedExternalBoneWorldTransforms
    void SyncAnimToPhysicsPose(RagdollInstance& inst);

    // Blend 模式：混合动画和物理变换 → FeedExternalBoneWorldTransforms
    void BlendAndApplyPose(RagdollInstance& inst);

    // 坐标转换
    static glm::mat4   PxToGlm(const PxTransform& t);
    static PxTransform GlmToPx(const glm::mat4& m);

    // 模型空间骨骼矩阵逐骨 lerp/slerp
    static void BlendModelTransforms(const std::vector<glm::mat4>& a,
                                     const std::vector<glm::mat4>& b,
                                     float t,
                                     std::vector<glm::mat4>& out);

    std::vector<RagdollInstance> m_Instances;
};
```

---

## 5. 需要对现有类的最小改动

### 5.1 VansAnimationController — 新增 FeedExternalBoneWorldTransforms

```cpp
// 头文件新增（public 区）
void FeedExternalBoneWorldTransforms(
    const std::vector<glm::mat4>& modelSpaceTransforms,
    const Skeleton& skeleton);
```

```cpp
// 实现
void VansAnimationController::FeedExternalBoneWorldTransforms(
    const std::vector<glm::mat4>& modelSpaceTransforms,
    const Skeleton& skeleton)
{
    if (modelSpaceTransforms.size() != skeleton.bones.size()) return;
    m_CachedGlobalTransforms = modelSpaceTransforms;
    BuildFinalMatrices(modelSpaceTransforms, skeleton);
}
```

- `BuildFinalMatrices` 已是 private，保持不变，直接在本函数中调用
- 不修改状态机、clip 时间或任何其他成员

### 5.2 VansScriptContext — 新增 VansScriptRagdollComponent

```cpp
class VansScriptRagdollComponent : public VansScriptComponent
{
public:
    VansScriptRagdollComponent() { m_ComponentName = "Ragdoll"; }

    VansAnimationNode*    m_AnimNode        = nullptr;
    RagdollDriveMode      m_InitialDriveMode = RagdollDriveMode::Animation;

    // Python/C++ 接口
    void SetDriveMode(int mode);       // 0=Animation 1=Physics 2=Blend
    void SetBlendWeight(float weight);
    void ApplyImpulse(const std::string& boneName, float ix, float iy, float iz);
};
```

### 5.3 VansScene::UpdateAnimations — 插入钩子

```cpp
void VansScene::UpdateAnimations(float deltaTime)
{
    for (VansAnimationNode* animNode : m_AnimationNodes)
    {
        if (!animNode) continue;
        animNode->Update(deltaTime);
        // ← NEW: ragdoll 在动画更新后、GPU 上传前介入
        VansRagdollSystem::GetInstance().PostAnimationUpdate(animNode);
        animNode->UploadBoneMatrices(0);
    }
}
```

### 5.4 VansScene::UnLoadScene — 在物理节点析构前释放 ragdoll

```cpp
// 在 "std::lock_guard<std::mutex> simLock(physicsSystem.GetSimulationMutex());" 之前
VansRagdollSystem::GetInstance().Shutdown();
```

### 5.5 VansSceneLoader — 新增 LoadSingleRagdollComponent

在 `LoadSingleAnimationComponent` 成功后、第四遍循环末尾检查 `animation.ragdoll` 字段并调用新函数：

```cpp
if (animNode && pending.animJson.contains("ragdoll"))
    LoadSingleRagdollComponent(obj, animNode, pending.animJson["ragdoll"], projectRoot);
```

`LoadSingleRagdollComponent` 流程：
1. 读取 `profile` 路径，调用 `RagdollProfile::LoadFromFile`
2. 解析 `drive_mode` / `blend_weight`
3. 若 `GetCachedGlobalTransforms().empty()`，调一次 `controller->Update(0.0f, skeleton)` 确保 bind pose 可用
4. 调用 `VansRagdollSystem::GetInstance().CreateRagdoll(animNode, profile)`
5. 应用初始驱动模式/权重
6. 在 `VansScriptObject` 上挂载 `VansScriptRagdollComponent`

---

## 6. 核心算法

### 6.1 CreateRagdoll（体 + 关节创建）

```
对每个 RagdollBodyConfig:
  1. 从 skeleton.boneNameToIndex 查找 boneIndex
  2. 计算 boneWorld = rootWorld * globalTransforms[boneIndex]
  3. 计算 shapeOffset = TRS(offsetPosition, offsetRotation, {1,1,1})
  4. 初始体位姿 bodyWorld = boneWorld * shapeOffset
  5. PxRigidDynamic::create(GlmToPx(bodyWorld))
  6. 创建形状（Capsule/Box/Sphere），设置过滤数据（layer 系统）
  7. 设置 eKINEMATIC = true（默认 Animation 模式）
  8. 加入 PxScene

对每个 RagdollJointConfig:
  1. 找到子骨骼的 entry
  2. 沿 skeleton 父链向上找最近的有体的骨骼（父体）
  3. 计算 parentFrame / childFrame（joint pivot 在子体世界原点）
  4. PxD6JointCreate(parentBody, parentFrame, childBody, childFrame)
  5. 设置 eLOCKED(X/Y/Z) + eLIMITED(SWING1/SWING2/TWIST)
  6. 设置摆动限和扭转限（带弹簧）
  7. 若 enableDrive，设置 D6Drive（SLERP/SWING/TWIST）
  8. ePROJECTION = true，projectionTolerance
```

### 6.2 SyncBodiesToAnimPose（Animation 模式，每帧）

```
对每个 entry:
  bodyWorld = rootWorld * globalTransforms[entry.boneIndex] * entry.shapeOffset
  body->setKinematicTarget(GlmToPx(bodyWorld))
```

### 6.3 SyncAnimToPhysicsPose（Physics 模式，每帧）

```
对每个 entry:
  bodyWorld = PxToGlm(body->getGlobalPose())
  boneWorld = bodyWorld * entry.shapeOffsetInverse
  modelTransforms[entry.boneIndex] = rootWorldInverse * boneWorld

controller->FeedExternalBoneWorldTransforms(modelTransforms, skeleton)
```

### 6.4 BlendAndApplyPose（Blend 模式，每帧）

```
animTransforms = controller->GetCachedGlobalTransforms()  // 动画输出

对每个 entry:
  physTransforms[entry.boneIndex] = 物理体反算的模型空间矩阵（同 Physics 模式）

对每个骨骼 i:
  decompose animTransforms[i] → posA, rotA, scaleA
  decompose physTransforms[i] → posB, rotB, scaleB
  blended[i] = TRS(mix(posA,posB,w), slerp(rotA,rotB,w), mix(scaleA,scaleB,w))

controller->FeedExternalBoneWorldTransforms(blended, skeleton)
```

### 6.5 SetDriveMode 切换逻辑

```
Animation → Physics/Blend:
  对所有 body: setRigidBodyFlag(eKINEMATIC, false)  // 释放物理模拟

Physics/Blend → Animation:
  对所有 body: setRigidBodyFlag(eKINEMATIC, true)   // 重回 Kinematic 跟随
```

---

## 7. 资产格式

### 7.1 .vragdoll（Profile JSON）

```json
{
  "name": "HeroRagdoll",
  "bodies": [
    {
      "bone_name": "pelvis",
      "shape_type": "capsule",
      "capsule_radius": 0.14,
      "capsule_half_height": 0.08,
      "mass": 12.0,
      "static_friction": 0.5,
      "dynamic_friction": 0.4,
      "restitution": 0.05,
      "layer": "Character"
    },
    {
      "bone_name": "spine_01",
      "shape_type": "capsule",
      "capsule_radius": 0.12,
      "capsule_half_height": 0.15,
      "mass": 10.0,
      "offset_position": [0, 0.05, 0]
    },
    {
      "bone_name": "upperarm_l",
      "shape_type": "capsule",
      "capsule_radius": 0.05,
      "capsule_half_height": 0.12,
      "mass": 3.0,
      "offset_rotation": [0, 0, 90]
    }
  ],
  "joints": [
    {
      "child_bone": "spine_01",
      "swing_y_limit": 20.0,
      "swing_z_limit": 15.0,
      "twist_low_limit": -10.0,
      "twist_high_limit": 10.0,
      "limit_stiffness": 20.0,
      "limit_damping": 2.0
    },
    {
      "child_bone": "upperarm_l",
      "swing_y_limit": 70.0,
      "swing_z_limit": 50.0,
      "twist_low_limit": -60.0,
      "twist_high_limit": 60.0,
      "enable_drive": true,
      "drive_stiffness": 50.0,
      "drive_damping": 5.0
    }
  ]
}
```

### 7.2 scene.json — animation 组件中的 ragdoll 字段

```json
{
  "name": "Hero",
  "components": {
    "animation": {
      "mesh_group": "hero_mesh",
      "animator": "Assets/Animators/hero.vanimator",
      "ragdoll": {
        "profile": "Assets/Ragdolls/hero.vragdoll",
        "drive_mode": "animation",
        "blend_weight": 0.0
      }
    }
  }
}
```

`drive_mode` 可选值：`"animation"` | `"physics"` | `"blend"`

---

## 8. 运行时脚本 API

### C++

```cpp
// 切换为布娃娃模式（死亡效果）
VansRagdollSystem::GetInstance().SetDriveMode(animNode, RagdollDriveMode::Physics);

// 对脊椎施加击飞冲量
VansRagdollSystem::GetInstance().ApplyImpulse(animNode, "spine_01",
                                               glm::vec3(0.0f, 800.0f, 300.0f));

// 混合过渡（受击效果）
VansRagdollSystem::GetInstance().SetDriveMode(animNode, RagdollDriveMode::Blend);
VansRagdollSystem::GetInstance().SetBlendWeight(animNode, 0.6f);

// 恢复动画驱动
VansRagdollSystem::GetInstance().SetDriveMode(animNode, RagdollDriveMode::Animation);
```

### Python（通过 VansScriptRagdollComponent）

```python
ragdoll = self.GetComponent("Ragdoll")

# 死亡时切换全物理
ragdoll.SetDriveMode(1)
ragdoll.ApplyImpulse("spine_01", 0, 800, 300)

# Blend 模式受击晃动
ragdoll.SetDriveMode(2)
ragdoll.SetBlendWeight(0.7)

# 恢复动画
ragdoll.SetDriveMode(0)
```

---

## 9. 新增文件清单

| 文件 | 说明 |
|------|------|
| `Source/EngineCore/PhysicsCore/VansRagdollTypes.h` | 所有数据结构 |
| `Source/EngineCore/PhysicsCore/VansRagdollSystem.h` | 系统类声明 |
| `Source/EngineCore/PhysicsCore/VansRagdollSystem.cpp` | 系统实现 |

---

## 10. 需改动的现有文件

| 文件 | 改动说明 |
|------|---------|
| `AnimationCore/VansAnimationController.h` | 新增 `FeedExternalBoneWorldTransforms` 公开方法声明 |
| `AnimationCore/VansAnimationController.cpp` | 实现 `FeedExternalBoneWorldTransforms` |
| `ScriptCore/VansScriptContext.h` | 新增 `VansScriptRagdollComponent` 类 |
| `ScriptCore/VansScriptContext.cpp` | 实现 `VansScriptRagdollComponent` 的三个方法 |
| `RenderCore/VansScene.h` | include `VansRagdollSystem.h`；声明 `LoadSingleRagdollComponent` |
| `RenderCore/VansScene.cpp` | `UpdateAnimations` 插入 `PostAnimationUpdate` 调用；`UnLoadScene` 调用 `Shutdown` |
| `RenderCore/VansSceneLoader.cpp` | include；实现 `LoadSingleRagdollComponent`；第四遍循环中调用 |
| `ForestEngine.vcxproj` | 注册三个新文件的 ClInclude / ClCompile 条目 |

---

## 11. 线程安全注意事项

- `VansPhysicsSystem` 已有 `GetSimulationMutex()`，所有 PhysX 写操作（`addActor`、`removeActor`、`setKinematicTarget`、`setRigidBodyFlag`、`addForce`）都必须在持有该锁的情况下执行
- `PostAnimationUpdate` 在主线程调用，物理 Step 在独立线程，两者通过 `SimulationMutex` 同步
- `Shutdown` 在持锁块**之外**先调用自己的锁来释放 D6 joint，再进入主锁块析构 actor，避免死锁

---

## 12. 已知限制（MVP 阶段）

1. 关节父体通过骨骼层级自动查找，不支持跳级关节（如骨骼 A→C 跳过 B）
2. 碰撞形状只支持 Capsule / Box / Sphere，不支持 ConvexHull
3. Blend 模式当前以匀速线性混合；高质量方案需引入曲线或 Additive Layer
4. `ApplyImpulse` 只对 Physics/Blend 模式有效；Animation 模式下体为 Kinematic，PhysX 会忽略外力
5. 不支持多层 Ragdoll Profile（如上半身物理 + 下半身动画的分层控制），需二期通过骨骼掩码实现
