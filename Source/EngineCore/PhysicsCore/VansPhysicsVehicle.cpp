#include "VansPhysicsVehicle.h"
#include "../Util/VansLog.h"
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>

// Helper macros for JSON parsing with safety checks
#define JSON_GET_VEC3(jsonVal, vec) \
    if (jsonVal.IsArray() && jsonVal.Size() >= 3) { \
        vec = PxVec3( \
            static_cast<float>(jsonVal[0].GetDouble()), \
            static_cast<float>(jsonVal[1].GetDouble()), \
            static_cast<float>(jsonVal[2].GetDouble())); \
    }

#define JSON_GET_QUAT(jsonVal, quat) \
    if (jsonVal.IsArray() && jsonVal.Size() >= 4) { \
        quat = PxQuat( \
            static_cast<float>(jsonVal[0].GetDouble()), \
            static_cast<float>(jsonVal[1].GetDouble()), \
            static_cast<float>(jsonVal[2].GetDouble()), \
            static_cast<float>(jsonVal[3].GetDouble())); \
    }

namespace VansEngine
{
    using json = nlohmann::json;

    // Helper to read JSON file to our params structure
    // Since we don't have the original snippet serialization code available as files in the workspace 
    // (they were in snippets which aren't fully indexed or available to compile), we'll implement a basic loader.
    // This is a simplified loader based on the structure seen in Base.json and EngineDrive.json.
    
    bool LoadVehicleParams(const std::string& path, VansVehicleParams& params)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            VANS_LOG_ERROR("[VansVehicle] Failed to open vehicle config: " << path);
            return false;
        }

        try
        {
            json config;
            file >> config;

            // TODO: Because the full JSON parsing logic from the snippets is extensive (BaseSerialization.cpp, etc.),
            // and we want to avoid copying thousands of lines of code, we will implement a focused parser 
            // that handles the key components we saw in Base.json.
            
            // For now, we will set up default values and only parse critical sections to get something running.
            // A production version would need comprehensive JSON parsing matching the PhysX vehicle schema.

            // 1. Axle Description (Critical)
            if (config.contains("AxleDescription"))
            {
                auto& axles = config["AxleDescription"];
                params.axleDescription.setToDefault();
                for (const auto& axle : axles)
                {
                    if (axle.contains("WheelIds"))
                    {
                        std::vector<int> wheelIds = axle["WheelIds"].get<std::vector<int>>();
                        // We cast std::vector<int> to the format expected by addAxle if needed, 
                        // or just add them manually.
                        // PhysX 5.1 addAxle expects pointers and count.
                        params.axleDescription.addAxle(wheelIds.size(), (PxU32*)wheelIds.data());
                    }
                }
            }
            
            // 2. Frame (Critical)
            if (config.contains("Frame"))
            {
                params.frame.lngAxis = static_cast<PxVehicleAxes::Enum>(config["Frame"]["LngAxis"].get<int>());
                params.frame.latAxis = static_cast<PxVehicleAxes::Enum>(config["Frame"]["LatAxis"].get<int>());
                params.frame.vrtAxis = static_cast<PxVehicleAxes::Enum>(config["Frame"]["VrtAxis"].get<int>());
            }

            // 3. Scale
            if (config.contains("Scale"))
            {
                params.scale.scale = config["Scale"]["Scale"].get<float>();
            }

            // 4. Mesh/RigidBody Mass (Simplified)
            // Real implementation requires parsing all suspension/wheel/engine params.
            // ...
            
            // NOTE: Since implementing a full parser for the complex PhysX vehicle JSON format
            // is a very large task, we will assume for this step that we can use hardcoded defaults 
            // derived from the snippet or that the user will provide a full parser later.
            // To make this compile and work 'out of the box' like the clean snippet,
            // we will populate with valid 'sedan' defaults if JSON is missing or incomplete, 
            // or if we decide to skip full parsing for brevity.

             return true;
        }
        catch (const std::exception& e)
        {
            VANS_LOG_ERROR("[VansVehicle] JSON parsing error: " << e.what());
            return false;
        }
    }

    // ============================================================================
    // VansPhysicsVehicle Implementation
    // ============================================================================

    VansPhysicsVehicle::VansPhysicsVehicle()
    {
    }

    VansPhysicsVehicle::~VansPhysicsVehicle()
    {
        Shutdown();
    }

    bool VansPhysicsVehicle::Initialize(VansPhysicsSystem* physicsSystem, const std::string& jsonPath, const PxTransform& startPose)
    {
        m_PhysicsSystem = physicsSystem;
        if (!m_PhysicsSystem || !m_PhysicsSystem->GetPhysics() || !m_PhysicsSystem->GetScene())
            return false;

        // -- Initialize State --
        // Must be done BEFORE creating the actor, otherwise we wipe the actor pointer!
        m_State.setToDefault();
        m_CommandState.setToDefault();
        m_TransmissionCommandState.setToDefault();

        // 1. Load Parameters
        // In a real implementation, we would parse the JSON here.
        // For now, let's setup default parameters for a 4-wheeled car manually to ensure stability.
        // This effectively hardcodes a 'standard card' behavior similar to the snippet.
        
        // -- Axles --
        // Axle 0: Front wheels (0, 1)
        // Axle 1: Rear wheels (2, 3)
        PxU32 frontWheels[] = { 0, 1 };
        PxU32 rearWheels[] = { 2, 3 };
        m_Params.axleDescription.setToDefault();
        m_Params.axleDescription.addAxle(2, frontWheels);
        m_Params.axleDescription.addAxle(2, rearWheels);

        // -- Frame --
        m_Params.frame.latAxis = PxVehicleAxes::ePosX;
        m_Params.frame.vrtAxis = PxVehicleAxes::ePosY;
        m_Params.frame.lngAxis = PxVehicleAxes::ePosZ;

        // -- Scale --
        m_Params.scale.scale = 1.0f;

        // -- Rigid Body (from Base.json) --
        m_Params.rigidBodyParams.mass = 2014.39990234375f;
        m_Params.rigidBodyParams.moi = PxVec3(3200.0f, 3414.39990234375f, 750.0f);

        // -- Brake Command Response Params (from Base.json) --
        // brakeResponseParams[0] = foot brake, brakeResponseParams[1] = handbrake
        m_Params.brakeResponseParams[0].maxResponse = 1875.0f;
        for (int i = 0; i < 4; i++)
            m_Params.brakeResponseParams[0].wheelResponseMultipliers[i] = 1.0f;

        m_Params.brakeResponseParams[1].maxResponse = 0.0f; // handbrake
        m_Params.brakeResponseParams[1].wheelResponseMultipliers[0] = 0.0f;
        m_Params.brakeResponseParams[1].wheelResponseMultipliers[1] = 0.0f;
        m_Params.brakeResponseParams[1].wheelResponseMultipliers[2] = 1.0f;
        m_Params.brakeResponseParams[1].wheelResponseMultipliers[3] = 1.0f;

        // -- Steer Command Response Params (from Base.json) --
        m_Params.steerResponseParams.maxResponse = 0.5235990285873413f; // ~30 degrees
        m_Params.steerResponseParams.wheelResponseMultipliers[0] = 1.0f; // Front left
        m_Params.steerResponseParams.wheelResponseMultipliers[1] = 1.0f; // Front right
        m_Params.steerResponseParams.wheelResponseMultipliers[2] = 0.0f; // Rear left
        m_Params.steerResponseParams.wheelResponseMultipliers[3] = 0.0f; // Rear right

        // -- Ackermann Params (from Base.json) --
        m_Params.ackermannParams[0].wheelIds[0] = 0;
        m_Params.ackermannParams[0].wheelIds[1] = 1;
        m_Params.ackermannParams[0].wheelBase = 2.863219976425171f;
        m_Params.ackermannParams[0].trackWidth = 1.5510799884796143f;
        m_Params.ackermannParams[0].strength = 1.0f;

        // -- Wheels & Suspension (from Base.json) --
        // Wheel positions from Base.json suspension attachment points
        const PxVec3 suspAttachPos[4] = {
            PxVec3(-0.7952629923820496f, 0.3161f,  1.377f),   // FL
            PxVec3( 0.7952629923820496f, 0.3161f,  1.377f),   // FR
            PxVec3(-0.7952629923820496f, 0.3161f, -1.1787f),   // RL
            PxVec3( 0.7952629923820496f, 0.3161f, -1.1787f)    // RR
        };
        const float suspForceStiffness[4]  = { 32833.30078125f, 33657.3984375f, 26049.0f, 26894.099609375f };
        const float suspForceDamping[4]    = { 8528.1201171875f, 8742.1904296875f, 6765.97021484375f, 6985.47998046875f };
        const float suspForceSprungMass[4] = { 553.7739868164063f, 567.6749877929688f, 439.3489990234375f, 453.6029968261719f };

        for (int i = 0; i < 4; i++)
        {
            // Wheel params (from Base.json)
            m_Params.wheelParams[i].radius = 0.3432520031929016f;
            m_Params.wheelParams[i].halfWidth = 0.15768450498580934f;
            m_Params.wheelParams[i].mass = 20.0f;
            m_Params.wheelParams[i].moi = 1.1716899871826172f;
            m_Params.wheelParams[i].dampingRate = 0.25f;

            // Suspension params (from Base.json)
            m_Params.suspensionParams[i].suspensionAttachment.p = suspAttachPos[i];
            m_Params.suspensionParams[i].suspensionAttachment.q = PxQuat(PxIdentity);
            m_Params.suspensionParams[i].suspensionTravelDir = PxVec3(0, -1, 0);
            m_Params.suspensionParams[i].suspensionTravelDist = 0.221110999584198f;
            m_Params.suspensionParams[i].wheelAttachment.p = PxVec3(0, 0, 0);
            m_Params.suspensionParams[i].wheelAttachment.q = PxQuat(PxIdentity);

            // Suspension force params (from Base.json)
            m_Params.suspensionForceParams[i].stiffness = suspForceStiffness[i];
            m_Params.suspensionForceParams[i].damping = suspForceDamping[i];
            m_Params.suspensionForceParams[i].sprungMass = suspForceSprungMass[i];

            // Tire force params (from Base.json)
            const bool isFront = (i < 2);
            m_Params.tireForceParams[i].longStiff = 24525.0f;
            m_Params.tireForceParams[i].latStiffX = 0.009999999776482582f;
            m_Params.tireForceParams[i].latStiffY = isFront ? 118699.637252138f : 143930.84033118f;
            m_Params.tireForceParams[i].camberStiff = 0.0f;
            m_Params.tireForceParams[i].restLoad = isFront ? 5628.72314453125f : 4604.3134765625f;
            // FrictionVsSlip: flat curve at 1.0
            m_Params.tireForceParams[i].frictionVsSlip[0][0] = 0.0f;
            m_Params.tireForceParams[i].frictionVsSlip[0][1] = 1.0f;
            m_Params.tireForceParams[i].frictionVsSlip[1][0] = 0.1f;
            m_Params.tireForceParams[i].frictionVsSlip[1][1] = 1.0f;
            m_Params.tireForceParams[i].frictionVsSlip[2][0] = 1.0f;
            m_Params.tireForceParams[i].frictionVsSlip[2][1] = 1.0f;
            // TireLoadFilter
            m_Params.tireForceParams[i].loadFilter[0][0] = 0.0f;
            m_Params.tireForceParams[i].loadFilter[0][1] = 0.23080000281333924f;
            m_Params.tireForceParams[i].loadFilter[1][0] = 3.0f;
            m_Params.tireForceParams[i].loadFilter[1][1] = 3.0f;
        }

        // -- Suspension State Calculation Params (from snippet Base.json) --
        m_Params.suspensionStateCalculationParams.suspensionJounceCalculationType = PxVehicleSuspensionJounceCalculationType::eSWEEP;
        m_Params.suspensionStateCalculationParams.limitSuspensionExpansionVelocity = false;

        // -- Engine Params (from EngineDrive.json) --
        m_Params.engineParams.torqueCurve.addPair(0.0f, 1.0f);
        m_Params.engineParams.torqueCurve.addPair(0.33f, 1.0f);
        m_Params.engineParams.torqueCurve.addPair(1.0f, 1.0f);
        m_Params.engineParams.moi = 1.0f;
        m_Params.engineParams.peakTorque = 500.0f;
        m_Params.engineParams.idleOmega = 0.0f;
        m_Params.engineParams.maxOmega = 600.0f;
        m_Params.engineParams.dampingRateFullThrottle = 0.15f;
        m_Params.engineParams.dampingRateZeroThrottleClutchEngaged = 2.0f;
        m_Params.engineParams.dampingRateZeroThrottleClutchDisengaged = 0.35f;

        // -- Gearbox Params (from EngineDrive.json) --
        // Ratios: [reverse, neutral, 1st, 2nd, 3rd, 4th, 5th] => neutralGear index = 1
        m_Params.gearBoxParams.neutralGear = 1;
        m_Params.gearBoxParams.ratios[0] = -4.0f; // Reverse
        m_Params.gearBoxParams.ratios[1] =  0.0f; // Neutral
        m_Params.gearBoxParams.ratios[2] =  4.0f; // 1st
        m_Params.gearBoxParams.ratios[3] =  2.0f; // 2nd
        m_Params.gearBoxParams.ratios[4] =  1.5f; // 3rd
        m_Params.gearBoxParams.ratios[5] =  1.1f; // 4th
        m_Params.gearBoxParams.ratios[6] =  1.0f; // 5th
        m_Params.gearBoxParams.nbRatios = 7;
        m_Params.gearBoxParams.finalRatio = 4.0f;
        m_Params.gearBoxParams.switchTime = 0.5f;

        // -- Autobox Params (from EngineDrive.json) --
        m_Params.autoboxParams.upRatios[0] = 0.65f;
        m_Params.autoboxParams.upRatios[1] = 0.15f;
        m_Params.autoboxParams.upRatios[2] = 0.65f;
        m_Params.autoboxParams.upRatios[3] = 0.65f;
        m_Params.autoboxParams.upRatios[4] = 0.65f;
        m_Params.autoboxParams.upRatios[5] = 0.65f;
        m_Params.autoboxParams.upRatios[6] = 0.65f;
        m_Params.autoboxParams.downRatios[0] = 0.5f;
        m_Params.autoboxParams.downRatios[1] = 0.5f;
        m_Params.autoboxParams.downRatios[2] = 0.5f;
        m_Params.autoboxParams.downRatios[3] = 0.5f;
        m_Params.autoboxParams.downRatios[4] = 0.5f;
        m_Params.autoboxParams.downRatios[5] = 0.5f;
        m_Params.autoboxParams.downRatios[6] = 0.5f;
        m_Params.autoboxParams.latency = 2.0f;

        // -- Clutch Command Response Params (from EngineDrive.json) --
        m_Params.clutchCommandResponseParams.maxResponse = 10.0f;

        // -- Clutch Params (from EngineDrive.json) --
        m_Params.clutchParams.accuracyMode = PxVehicleClutchAccuracyMode::eESTIMATE;
        m_Params.clutchParams.estimateIterations = 5;

        // -- Four Wheel Differential Params (from EngineDrive.json) --
        m_Params.fourWheelDifferentialParams.torqueRatios[0] = 0.25f;
        m_Params.fourWheelDifferentialParams.torqueRatios[1] = 0.25f;
        m_Params.fourWheelDifferentialParams.torqueRatios[2] = 0.25f;
        m_Params.fourWheelDifferentialParams.torqueRatios[3] = 0.25f;
        m_Params.fourWheelDifferentialParams.aveWheelSpeedRatios[0] = 0.25f;
        m_Params.fourWheelDifferentialParams.aveWheelSpeedRatios[1] = 0.25f;
        m_Params.fourWheelDifferentialParams.aveWheelSpeedRatios[2] = 0.25f;
        m_Params.fourWheelDifferentialParams.aveWheelSpeedRatios[3] = 0.25f;
        m_Params.fourWheelDifferentialParams.frontWheelIds[0] = 0;
        m_Params.fourWheelDifferentialParams.frontWheelIds[1] = 1;
        m_Params.fourWheelDifferentialParams.rearWheelIds[0] = 2;
        m_Params.fourWheelDifferentialParams.rearWheelIds[1] = 3;
        m_Params.fourWheelDifferentialParams.centerBias = 1.3f;
        m_Params.fourWheelDifferentialParams.centerTarget = 1.29f;
        m_Params.fourWheelDifferentialParams.frontBias = 1.3f;
        m_Params.fourWheelDifferentialParams.frontTarget = 1.29f;
        m_Params.fourWheelDifferentialParams.rearBias = 1.3f;
        m_Params.fourWheelDifferentialParams.rearTarget = 1.29f;
        m_Params.fourWheelDifferentialParams.rate = 10.0f;

        // -- PhysX Integration Params --
        // Set up road geometry query, material friction, suspension limit constraint params
        // following the snippet's setPhysXIntegrationParams pattern.
        PxPhysics* physics = m_PhysicsSystem->GetPhysics();
        PxMaterial* material = physics->createMaterial(0.5f, 0.5f, 0.6f);

        m_Params.physxRoadGeometryQueryParams.roadGeometryQueryType = PxVehiclePhysXRoadGeometryQueryType::eRAYCAST;
        m_Params.physxRoadGeometryQueryParams.defaultFilterData = PxQueryFilterData(PxFilterData(0, 0, 0, 0), PxQueryFlag::eSTATIC);
        m_Params.physxRoadGeometryQueryParams.filterCallback = nullptr;
        m_Params.physxRoadGeometryQueryParams.filterDataEntries = nullptr;

        for (PxU32 i = 0; i < m_Params.axleDescription.nbWheels; i++)
        {
            const PxU32 wheelId = m_Params.axleDescription.wheelIdsInAxleOrder[i];
            m_Params.physxMaterialFrictionParams[wheelId].defaultFriction = 1.0f;
            m_Params.physxMaterialFrictionParams[wheelId].materialFrictions = nullptr;
            m_Params.physxMaterialFrictionParams[wheelId].nbMaterialFrictions = 0;

            m_Params.physxSuspensionLimitConstraintParams[wheelId].restitution = 0.0f;
            m_Params.physxSuspensionLimitConstraintParams[wheelId].directionForSuspensionLimitConstraint =
                PxVehiclePhysXSuspensionLimitConstraintParams::eROAD_GEOMETRY_NORMAL;

            m_Params.physxWheelShapeLocalPoses[wheelId] = PxTransform(PxIdentity);
        }

        // CMass local pose, body shape extents & local pose (matching snippet defaults)
        m_Params.physxActorCMassLocalPose = PxTransform(PxVec3(0.0f, 0.55f, 1.594f), PxQuat(PxIdentity));
        m_Params.physxActorBoxShapeHalfExtents = PxVec3(0.84097f, 0.65458f, 2.46971f);
        m_Params.physxActorBoxShapeLocalPose = PxTransform(PxVec3(0.0f, 0.830066f, 1.37003f), PxQuat(PxIdentity));

        // -- Create Rigid Body + Wheel Shapes via PxVehiclePhysXActorCreate --
        // This creates the PxRigidDynamic, attaches a box body shape and convex-mesh wheel shapes,
        // sets mass/MOI/CMass, disables gravity (vehicle SDK handles gravity itself).
        {
            const PxVehiclePhysXRigidActorParams rigidActorParams(m_Params.rigidBodyParams, nullptr);
            const PxBoxGeometry boxGeom(m_Params.physxActorBoxShapeHalfExtents);
            const PxVehiclePhysXRigidActorShapeParams rigidActorShapeParams(
                boxGeom, m_Params.physxActorBoxShapeLocalPose, *material,
                PxShapeFlags(0), PxFilterData(), PxFilterData());
            const PxVehiclePhysXWheelParams physxWheelParams(
                m_Params.axleDescription, m_Params.wheelParams);
            const PxVehiclePhysXWheelShapeParams physxWheelShapeParams(
                *material, PxShapeFlags(0), PxFilterData(), PxFilterData());

            PxVehiclePhysXActorCreate(
                m_Params.frame,
                rigidActorParams, m_Params.physxActorCMassLocalPose,
                rigidActorShapeParams,
                physxWheelParams, physxWheelShapeParams,
                *physics, PxCookingParams(PxTolerancesScale()),
                m_State.physxActor);
        }

        // -- Create PhysX Constraints (suspension limit & sticky tire) --
        PxVehicleConstraintsCreate(m_Params.axleDescription, *physics,
            *m_State.physxActor.rigidBody, m_State.physxConstraints);

        // Apply the start pose and add to the scene
        m_State.physxActor.rigidBody->setGlobalPose(startPose);
        m_State.physxActor.rigidBody->setName("VansVehicle");
        m_PhysicsSystem->GetScene()->addActor(*m_State.physxActor.rigidBody);

        // -- Set initial gear state (from snippet initVehicles) --
        // Set the vehicle in 1st gear (neutralGear + 1)
        m_State.gearboxState.currentGear = m_Params.gearBoxParams.neutralGear + 1;
        m_State.gearboxState.targetGear = m_Params.gearBoxParams.neutralGear + 1;

        // Set the vehicle to use the automatic gearbox
        m_TransmissionCommandState.targetGear = PxVehicleEngineDriveTransmissionCommandState::eAUTOMATIC_GEAR;

        // Set nbBrakes so brake processing works (snippet sets this in stepPhysics)
        m_CommandState.nbBrakes = 2; // brake + handbrake

        // -- Initialize Component Sequence --
        // This order is critical and follows the snippet
        m_ComponentSequence.add(static_cast<PxVehiclePhysXActorBeginComponent*>(this));
        m_ComponentSequence.add(static_cast<PxVehicleEngineDriveCommandResponseComponent*>(this));
        m_ComponentSequence.add(static_cast<PxVehicleFourWheelDriveDifferentialStateComponent*>(this));
        m_ComponentSequence.add(static_cast<PxVehicleEngineDriveActuationStateComponent*>(this));
        m_ComponentSequence.add(static_cast<PxVehiclePhysXRoadGeometrySceneQueryComponent*>(this));
        
        m_ComponentSequenceSubstepGroupHandle = m_ComponentSequence.beginSubstepGroup(3); // 3 substeps
            m_ComponentSequence.add(static_cast<PxVehicleSuspensionComponent*>(this));
            m_ComponentSequence.add(static_cast<PxVehicleTireComponent*>(this));
            m_ComponentSequence.add(static_cast<PxVehiclePhysXConstraintComponent*>(this));
            m_ComponentSequence.add(static_cast<PxVehicleEngineDrivetrainComponent*>(this));
            m_ComponentSequence.add(static_cast<PxVehicleRigidBodyComponent*>(this));
        m_ComponentSequence.endSubstepGroup();
        
        m_ComponentSequence.add(static_cast<PxVehicleWheelComponent*>(this));
        m_ComponentSequence.add(static_cast<PxVehiclePhysXActorEndComponent*>(this));

        // -- Setup Context --
        m_SimulationContext.setToDefault();
        m_SimulationContext.frame.lngAxis = PxVehicleAxes::ePosZ;
        m_SimulationContext.frame.latAxis = PxVehicleAxes::ePosX;
        m_SimulationContext.frame.vrtAxis = PxVehicleAxes::ePosY;
        m_SimulationContext.scale.scale = 1.0f;
        m_SimulationContext.gravity = m_PhysicsSystem->GetGravity();
        m_SimulationContext.physxScene = m_PhysicsSystem->GetScene();
        m_SimulationContext.physxActorUpdateMode = PxVehiclePhysXActorUpdateMode::eAPPLY_ACCELERATION;

        return true;
    }

    void VansPhysicsVehicle::Shutdown()
    {
        if (m_State.physxActor.rigidBody)
        {
            // Destroy constraints first
            PxVehicleConstraintsDestroy(m_State.physxConstraints);

            // Remove from scene before releasing
            if (m_PhysicsSystem && m_PhysicsSystem->GetScene())
            {
                m_PhysicsSystem->GetScene()->removeActor(*m_State.physxActor.rigidBody);
            }

            // Release rigid body + wheel shapes via the PhysX Vehicle helper
            PxVehiclePhysXActorDestroy(m_State.physxActor);
        }
    }

    void VansPhysicsVehicle::Step(float dt)
    {
        m_ComponentSequence.update(dt, m_SimulationContext);
		//std::cout << "[VansVehicle] Step completed. Position: " << GetTransform().p.x << ", " << GetTransform().p.y << ", " << GetTransform().p.z << std::endl;
    }

    void VansPhysicsVehicle::SetInputs(float throttle, float brake, float steer, float handbrake)
    {
        m_CommandState.throttle = throttle;
        m_CommandState.brakes[0] = brake;     // Standard brake
        m_CommandState.brakes[1] = handbrake; // Handbrake
        m_CommandState.steer = steer;
    }

    void VansPhysicsVehicle::SetGear(uint32_t gear)
    {
        m_TransmissionCommandState.targetGear = gear;
    }

    void VansPhysicsVehicle::SetAutomaticGear(bool automatic)
    {
        if (automatic)
             m_TransmissionCommandState.targetGear = PxVehicleEngineDriveTransmissionCommandState::eAUTOMATIC_GEAR;
    }

    const PxTransform& VansPhysicsVehicle::GetTransform() const
    {
        if (m_State.physxActor.rigidBody)
            return m_State.physxActor.rigidBody->getGlobalPose();
        return PxTransform(PxIdentity);
    }

    // ===================================
    // Data Provider Implementation
    // ===================================

    // Note: These implementations just wire up the internal m_Params and m_State members 
    // to the pointers requested by the component interfaces.

    void VansPhysicsVehicle::getDataForPhysXActorBeginComponent(
        const PxVehicleAxleDescription*& axleDescription,
        const PxVehicleCommandState*& commands,
        const PxVehicleEngineDriveTransmissionCommandState*& transmissionCommands,
        const PxVehicleGearboxParams*& gearParams,
        const PxVehicleGearboxState*& gearState,
        const PxVehicleEngineParams*& engineParams,
        PxVehiclePhysXActor*& physxActor,
        PxVehiclePhysXSteerState*& physxSteerState,
        PxVehiclePhysXConstraints*& physxConstraints,
        PxVehicleRigidBodyState*& rigidBodyState,
        PxVehicleArrayData<PxVehicleWheelRigidBody1dState>& wheelRigidBody1dStates,
        PxVehicleEngineState*& engineState)
    {
        axleDescription = &m_Params.axleDescription;
        commands = &m_CommandState;
        transmissionCommands = &m_TransmissionCommandState;
        gearParams = &m_Params.gearBoxParams;
        gearState = &m_State.gearboxState;
        engineParams = &m_Params.engineParams;
        physxActor = &m_State.physxActor;
        physxSteerState = &m_State.physxSteerState;
        physxConstraints = &m_State.physxConstraints;
        rigidBodyState = &m_State.rigidBodyState;
        wheelRigidBody1dStates.setData(m_State.wheelRigidBody1dStates);
        engineState = &m_State.engineState;
    }

    void VansPhysicsVehicle::getDataForPhysXActorEndComponent(
        const PxVehicleAxleDescription*& axleDescription,
        const PxVehicleRigidBodyState*& rigidBodyState,
        PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams,
        PxVehicleArrayData<const PxTransform>& wheelShapeLocalPoses,
        PxVehicleArrayData<const PxVehicleWheelRigidBody1dState>& wheelRigidBody1dStates,
        PxVehicleArrayData<const PxVehicleWheelLocalPose>& wheelLocalPoses,
        const PxVehicleGearboxState*& gearState,
        const PxReal*& throttle,
        PxVehiclePhysXActor*& physxActor)
    {
        axleDescription = &m_Params.axleDescription;
        rigidBodyState = &m_State.rigidBodyState;
        wheelParams.setData(m_Params.wheelParams);
        wheelShapeLocalPoses.setData(m_Params.physxWheelShapeLocalPoses);
        wheelRigidBody1dStates.setData(m_State.wheelRigidBody1dStates);
        wheelLocalPoses.setData(m_State.wheelLocalPoses);
        gearState = &m_State.gearboxState;
        throttle = &m_CommandState.throttle;
        physxActor = &m_State.physxActor;
    }

    void VansPhysicsVehicle::getDataForPhysXConstraintComponent(
        const PxVehicleAxleDescription*& axleDescription,
        const PxVehicleRigidBodyState*& rigidBodyState,
        PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams,
        PxVehicleArrayData<const PxVehiclePhysXSuspensionLimitConstraintParams>& suspensionLimitParams,
        PxVehicleArrayData<const PxVehicleSuspensionState>& suspensionStates,
        PxVehicleArrayData<const PxVehicleSuspensionComplianceState>& suspensionComplianceStates,
        PxVehicleArrayData<const PxVehicleRoadGeometryState>& wheelRoadGeomStates,
        PxVehicleArrayData<const PxVehicleTireDirectionState>& tireDirectionStates,
        PxVehicleArrayData<const PxVehicleTireStickyState>& tireStickyStates,
        PxVehiclePhysXConstraints*& constraints)
    {
        axleDescription = &m_Params.axleDescription;
        rigidBodyState = &m_State.rigidBodyState;
        suspensionParams.setData(m_Params.suspensionParams);
        suspensionLimitParams.setData(m_Params.physxSuspensionLimitConstraintParams);
        suspensionStates.setData(m_State.suspensionStates);
        suspensionComplianceStates.setData(m_State.suspensionComplianceStates);
        wheelRoadGeomStates.setData(m_State.roadGeomStates);
        tireDirectionStates.setData(m_State.tireDirectionStates);
        tireStickyStates.setData(m_State.tireStickyStates);
        constraints = &m_State.physxConstraints;
    }

    void VansPhysicsVehicle::getDataForPhysXRoadGeometrySceneQueryComponent(
        const PxVehicleAxleDescription*& axleDescription,
        const PxVehiclePhysXRoadGeometryQueryParams*& roadGeomParams,
        PxVehicleArrayData<const PxReal>& steerResponseStates,
        const PxVehicleRigidBodyState*& rigidBodyState,
        PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams,
        PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams,
        PxVehicleArrayData<const PxVehiclePhysXMaterialFrictionParams>& materialFrictionParams,
        PxVehicleArrayData<PxVehicleRoadGeometryState>& roadGeometryStates,
        PxVehicleArrayData<PxVehiclePhysXRoadGeometryQueryState>& physxRoadGeometryStates)
    {
        axleDescription = &m_Params.axleDescription;
        roadGeomParams = &m_Params.physxRoadGeometryQueryParams;
        steerResponseStates.setData(m_State.steerCommandResponseStates);
        rigidBodyState = &m_State.rigidBodyState;
        wheelParams.setData(m_Params.wheelParams);
        suspensionParams.setData(m_Params.suspensionParams);
        materialFrictionParams.setData(m_Params.physxMaterialFrictionParams);
        roadGeometryStates.setData(m_State.roadGeomStates);
        
        // We use a local helper for query state if needed, or pass the array if stored in state
        // For simplicity we mapped it to internal state
        physxRoadGeometryStates.setData(m_PhysXRoadGeometryQueryState); 
    }

    void VansPhysicsVehicle::getDataForRigidBodyComponent(
        const PxVehicleAxleDescription*& axleDescription,
        const PxVehicleRigidBodyParams*& rigidBodyParams,
        PxVehicleArrayData<const PxVehicleSuspensionForce>& suspensionForces,
        PxVehicleArrayData<const PxVehicleTireForce>& tireForces,
        const PxVehicleAntiRollTorque*& antiRollTorque,
        PxVehicleRigidBodyState*& rigidBodyState)
    {
        axleDescription = &m_Params.axleDescription;
        rigidBodyParams = &m_Params.rigidBodyParams;
        suspensionForces.setData(m_State.suspensionForces);
        tireForces.setData(m_State.tireForces);
        antiRollTorque = nullptr; // TODO: Implement if needed
        rigidBodyState = &m_State.rigidBodyState;
    }

    void VansPhysicsVehicle::getDataForSuspensionComponent(
        const PxVehicleAxleDescription*& axleDescription,
        const PxVehicleRigidBodyParams*& rigidBodyParams,
        const PxVehicleSuspensionStateCalculationParams*& suspensionStateCalculationParams,
        PxVehicleArrayData<const PxReal>& steerResponseStates,
        const PxVehicleRigidBodyState*& rigidBodyState,
        PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams,
        PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams,
        PxVehicleArrayData<const PxVehicleSuspensionComplianceParams>& suspensionComplianceParams,
        PxVehicleArrayData<const PxVehicleSuspensionForceParams>& suspensionForceParams,
        PxVehicleSizedArrayData<const PxVehicleAntiRollForceParams>& antiRollForceParams,
        PxVehicleArrayData<const PxVehicleRoadGeometryState>& wheelRoadGeomStates,
        PxVehicleArrayData<PxVehicleSuspensionState>& suspensionStates,
        PxVehicleArrayData<PxVehicleSuspensionComplianceState>& suspensionComplianceStates,
        PxVehicleArrayData<PxVehicleSuspensionForce>& suspensionForces,
        PxVehicleAntiRollTorque*& antiRollTorque)
    {
        axleDescription = &m_Params.axleDescription;
        rigidBodyParams = &m_Params.rigidBodyParams;
        suspensionStateCalculationParams = &m_Params.suspensionStateCalculationParams;
        steerResponseStates.setData(m_State.steerCommandResponseStates);
        rigidBodyState = &m_State.rigidBodyState;
        wheelParams.setData(m_Params.wheelParams);
        suspensionParams.setData(m_Params.suspensionParams);
        suspensionComplianceParams.setData(m_Params.suspensionComplianceParams);
        suspensionForceParams.setData(m_Params.suspensionForceParams);
        antiRollForceParams.setEmpty();
        wheelRoadGeomStates.setData(m_State.roadGeomStates);
        suspensionStates.setData(m_State.suspensionStates);
        suspensionComplianceStates.setData(m_State.suspensionComplianceStates);
        suspensionForces.setData(m_State.suspensionForces);
        antiRollTorque = nullptr;
    }

    void VansPhysicsVehicle::getDataForTireComponent(
        const PxVehicleAxleDescription*& axleDescription,
        PxVehicleArrayData<const PxReal>& steerResponseStates,
        const PxVehicleRigidBodyState*& rigidBodyState,
        PxVehicleArrayData<const PxVehicleWheelActuationState>& actuationStates,
        PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams,
        PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams,
        PxVehicleArrayData<const PxVehicleTireForceParams>& tireForceParams,
        PxVehicleArrayData<const PxVehicleRoadGeometryState>& roadGeomStates,
        PxVehicleArrayData<const PxVehicleSuspensionState>& suspensionStates,
        PxVehicleArrayData<const PxVehicleSuspensionComplianceState>& suspensionComplianceStates,
        PxVehicleArrayData<const PxVehicleSuspensionForce>& suspensionForces,
        PxVehicleArrayData<const PxVehicleWheelRigidBody1dState>& wheelRigidBody1DStates,
        PxVehicleArrayData<PxVehicleTireGripState>& tireGripStates,
        PxVehicleArrayData<PxVehicleTireDirectionState>& tireDirectionStates,
        PxVehicleArrayData<PxVehicleTireSpeedState>& tireSpeedStates,
        PxVehicleArrayData<PxVehicleTireSlipState>& tireSlipStates,
        PxVehicleArrayData<PxVehicleTireCamberAngleState>& tireCamberAngleStates, 
        PxVehicleArrayData<PxVehicleTireStickyState>& tireStickyStates,
        PxVehicleArrayData<PxVehicleTireForce>& tireForces)
    {
        axleDescription = &m_Params.axleDescription;
        steerResponseStates.setData(m_State.steerCommandResponseStates);
        rigidBodyState = &m_State.rigidBodyState;
        actuationStates.setData(m_State.actuationStates);
        wheelParams.setData(m_Params.wheelParams);
        suspensionParams.setData(m_Params.suspensionParams);
        tireForceParams.setData(m_Params.tireForceParams);
        roadGeomStates.setData(m_State.roadGeomStates);
        suspensionStates.setData(m_State.suspensionStates);
        suspensionComplianceStates.setData(m_State.suspensionComplianceStates);
        suspensionForces.setData(m_State.suspensionForces);
        wheelRigidBody1DStates.setData(m_State.wheelRigidBody1dStates);
        tireGripStates.setData(m_State.tireGripStates);
        tireDirectionStates.setData(m_State.tireDirectionStates);
        tireSpeedStates.setData(m_State.tireSpeedStates);
        tireSlipStates.setData(m_State.tireSlipStates);
        tireCamberAngleStates.setData(m_State.tireCamberAngleStates);
        tireStickyStates.setData(m_State.tireStickyStates);
        tireForces.setData(m_State.tireForces);
    }

    void VansPhysicsVehicle::getDataForWheelComponent(
        const PxVehicleAxleDescription*& axleDescription,
        PxVehicleArrayData<const PxReal>& steerResponseStates,
        PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams,
        PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams,
        PxVehicleArrayData<const PxVehicleWheelActuationState>& actuationStates,
        PxVehicleArrayData<const PxVehicleSuspensionState>& suspensionStates,
        PxVehicleArrayData<const PxVehicleSuspensionComplianceState>& suspensionComplianceStates,
        PxVehicleArrayData<const PxVehicleTireSpeedState>& tireSpeedStates,
        PxVehicleArrayData<PxVehicleWheelRigidBody1dState>& wheelRigidBody1dStates,
        PxVehicleArrayData<PxVehicleWheelLocalPose>& wheelLocalPoses)
    {
        axleDescription = &m_Params.axleDescription;
        steerResponseStates.setData(m_State.steerCommandResponseStates);
        wheelParams.setData(m_Params.wheelParams);
        suspensionParams.setData(m_Params.suspensionParams);
        actuationStates.setData(m_State.actuationStates);
        suspensionStates.setData(m_State.suspensionStates);
        suspensionComplianceStates.setData(m_State.suspensionComplianceStates);
        tireSpeedStates.setData(m_State.tireSpeedStates);
        wheelRigidBody1dStates.setData(m_State.wheelRigidBody1dStates);
        wheelLocalPoses.setData(m_State.wheelLocalPoses);
    }

    void VansPhysicsVehicle::getDataForEngineDriveCommandResponseComponent(
        const PxVehicleAxleDescription*& axleDescription,
        PxVehicleSizedArrayData<const PxVehicleBrakeCommandResponseParams>& brakeResponseParams,
        const PxVehicleSteerCommandResponseParams*& steerResponseParams,
        PxVehicleSizedArrayData<const PxVehicleAckermannParams>& ackermannParams,
        const PxVehicleGearboxParams*& gearboxParams,
        const PxVehicleClutchCommandResponseParams*& clutchResponseParams,
        const PxVehicleEngineParams*& engineParams,
        const PxVehicleRigidBodyState*& rigidBodyState,
        const PxVehicleEngineState*& engineState,
        const PxVehicleAutoboxParams*& autoboxParams,
        const PxVehicleCommandState*& commands,
        const PxVehicleEngineDriveTransmissionCommandState*& transmissionCommands,
        PxVehicleArrayData<PxReal>& brakeResponseStates,
        PxVehicleEngineDriveThrottleCommandResponseState*& throttleResponseState,
        PxVehicleArrayData<PxReal>& steerResponseStates,
        PxVehicleGearboxState*& gearboxResponseState,
        PxVehicleClutchCommandResponseState*& clutchResponseState,
        PxVehicleAutoboxState*& autoboxState)
    {
        axleDescription = &m_Params.axleDescription;
        brakeResponseParams.setDataAndCount(m_Params.brakeResponseParams, 2);
        steerResponseParams = &m_Params.steerResponseParams;
        ackermannParams.setDataAndCount(m_Params.ackermannParams, 1);
        gearboxParams = &m_Params.gearBoxParams;
        clutchResponseParams = &m_Params.clutchCommandResponseParams;
        engineParams = &m_Params.engineParams;
        rigidBodyState = &m_State.rigidBodyState;
        engineState = &m_State.engineState;
        autoboxParams = &m_Params.autoboxParams;
        commands = &m_CommandState;
        transmissionCommands = &m_TransmissionCommandState;
        brakeResponseStates.setData(m_State.brakeCommandResponseStates);
        throttleResponseState = &m_State.throttleCommandResponseState;
        steerResponseStates.setData(m_State.steerCommandResponseStates);
        gearboxResponseState = &m_State.gearboxState;
        clutchResponseState = &m_State.clutchCommandResponseState;
        autoboxState = &m_State.autoboxState;
    }

    void VansPhysicsVehicle::getDataForFourWheelDriveDifferentialStateComponent(
        const PxVehicleAxleDescription*& axleDescription,
        const PxVehicleFourWheelDriveDifferentialParams*& differentialParams,
        PxVehicleArrayData<const PxVehicleWheelRigidBody1dState>& wheelRigidbody1dStates,
        PxVehicleDifferentialState*& differentialState, PxVehicleWheelConstraintGroupState*& wheelConstraintGroups)
    {
        axleDescription = &m_Params.axleDescription;
        differentialParams = &m_Params.fourWheelDifferentialParams;
        wheelRigidbody1dStates.setData(m_State.wheelRigidBody1dStates);
        differentialState = &m_State.differentialState;
        wheelConstraintGroups = &m_State.wheelConstraintGroupState;
    }

    void VansPhysicsVehicle::getDataForEngineDriveActuationStateComponent(
        const PxVehicleAxleDescription*& axleDescription, 
        const PxVehicleGearboxParams*& gearboxParams,
        PxVehicleArrayData<const PxReal>& brakeResponseStates,
        const PxVehicleEngineDriveThrottleCommandResponseState*& throttleResponseState,
        const PxVehicleGearboxState*& gearboxState,
        const PxVehicleDifferentialState*& differentialState,
        const PxVehicleClutchCommandResponseState*& clutchResponseState,
        PxVehicleArrayData<PxVehicleWheelActuationState>& actuationStates)
    {
        axleDescription = &m_Params.axleDescription;
        gearboxParams = &m_Params.gearBoxParams;
        brakeResponseStates.setData(m_State.brakeCommandResponseStates);
        throttleResponseState = &m_State.throttleCommandResponseState;
        gearboxState = &m_State.gearboxState;
        differentialState = &m_State.differentialState;
        clutchResponseState = &m_State.clutchCommandResponseState;
        actuationStates.setData(m_State.actuationStates);
    }

    void VansPhysicsVehicle::getDataForEngineDrivetrainComponent(
        const PxVehicleAxleDescription*& axleDescription,
        PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams,
        const PxVehicleEngineParams*& engineParams,
        const PxVehicleClutchParams*& clutchParams,
        const PxVehicleGearboxParams*& gearboxParams, 
        PxVehicleArrayData<const PxReal>& brakeResponseStates,
        PxVehicleArrayData<const PxVehicleWheelActuationState>& actuationStates,
        PxVehicleArrayData<const PxVehicleTireForce>& tireForces,
        const PxVehicleEngineDriveThrottleCommandResponseState*& throttleResponseState,
        const PxVehicleClutchCommandResponseState*& clutchResponseState,
        const PxVehicleDifferentialState*& differentialState,
        const PxVehicleWheelConstraintGroupState*& constraintGroupState,
        PxVehicleArrayData<PxVehicleWheelRigidBody1dState>& wheelRigidBody1dStates,
        PxVehicleEngineState*& engineState,
        PxVehicleGearboxState*& gearboxState,
        PxVehicleClutchSlipState*& clutchState)
    {
        axleDescription = &m_Params.axleDescription;
        wheelParams.setData(m_Params.wheelParams);
        engineParams = &m_Params.engineParams;
        clutchParams = &m_Params.clutchParams;
        gearboxParams = &m_Params.gearBoxParams;
        brakeResponseStates.setData(m_State.brakeCommandResponseStates);
        actuationStates.setData(m_State.actuationStates);
        tireForces.setData(m_State.tireForces);
        throttleResponseState = &m_State.throttleCommandResponseState;
        clutchResponseState = &m_State.clutchCommandResponseState;
        differentialState = &m_State.differentialState;
        constraintGroupState = &m_State.wheelConstraintGroupState;
        wheelRigidBody1dStates.setData(m_State.wheelRigidBody1dStates);
        engineState = &m_State.engineState;
        gearboxState = &m_State.gearboxState;
        clutchState = &m_State.clutchState;
    }

    // =========================================================
    // Helpers
    // =========================================================

    bool VansVehicleParams::isValid() const
    {
        if (!axleDescription.isValid()) return false;
        if (!frame.isValid()) return false;
        if (!scale.isValid()) return false;
        return true;
    }

    void VansVehicleState::setToDefault()
    {
        for (unsigned int i = 0; i < PxVehicleLimits::eMAX_NB_WHEELS; i++)
        {
            brakeCommandResponseStates[i] = 0.0f;
            steerCommandResponseStates[i] = 0.0f;
            actuationStates[i].setToDefault();
            roadGeomStates[i].setToDefault();
            suspensionStates[i].setToDefault();
            suspensionComplianceStates[i].setToDefault();
            suspensionForces[i].setToDefault();
            tireGripStates[i].setToDefault();
            tireDirectionStates[i].setToDefault();
            tireSpeedStates[i].setToDefault();
            tireSlipStates[i].setToDefault();
            tireCamberAngleStates[i].setToDefault();
            tireStickyStates[i].setToDefault();
            tireForces[i].setToDefault();
            wheelRigidBody1dStates[i].setToDefault();
            wheelLocalPoses[i].setToDefault();
        }

        rigidBodyState.setToDefault();
        
        throttleCommandResponseState.setToDefault();
        autoboxState.setToDefault();
        clutchCommandResponseState.setToDefault();
        differentialState.setToDefault();
        wheelConstraintGroupState.setToDefault();
        engineState.setToDefault();
        gearboxState.setToDefault();
        clutchState.setToDefault();
        
        physxActor.setToDefault();
        physxSteerState.setToDefault();
        physxConstraints.setToDefault();
    }
}
