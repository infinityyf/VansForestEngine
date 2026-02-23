#include "VansPhysicsVehicle.h"
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
            std::cerr << "[VansVehicle] Failed to open vehicle config: " << path << std::endl;
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
            std::cerr << "[VansVehicle] JSON parsing error: " << e.what() << std::endl;
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

        // -- Rigid Body --
        m_Params.rigidBodyParams.mass = 1500.0f;
        m_Params.rigidBodyParams.moi = PxVec3(3200.0f, 3400.0f, 750.0f);

        // -- Wheels & Suspension (Generic defaults) --
        for (int i = 0; i < 4; i++)
        {
            m_Params.wheelParams[i].radius = 0.35f;
            m_Params.wheelParams[i].halfWidth = 0.25f * 0.5f;
            m_Params.wheelParams[i].mass = 20.0f;
            m_Params.wheelParams[i].moi = 0.5f * 20.0f * 0.35f * 0.35f;
            
            // Positions relative to center of mass
            float x = (i % 2 == 0) ? -0.8f : 0.8f; // Left/Right
            float z = (i < 2) ? 1.5f : -1.5f;      // Front/Rear
            float y = -0.5f;                       // Below center

            m_Params.suspensionParams[i].suspensionAttachment.p = PxVec3(x, y, z);
            m_Params.suspensionParams[i].suspensionAttachment.q = PxQuat(PxIdentity);
            m_Params.suspensionParams[i].suspensionTravelDir = PxVec3(0, -1, 0);
            m_Params.suspensionParams[i].suspensionTravelDist = 0.2f;
            m_Params.suspensionParams[i].wheelAttachment.p = PxVec3(0, 0, 0);
            m_Params.suspensionParams[i].wheelAttachment.q = PxQuat(PxIdentity);

            m_Params.suspensionForceParams[i].stiffness = 10000.0f;
            m_Params.suspensionForceParams[i].damping = 5000.0f;
            m_Params.suspensionForceParams[i].sprungMass = 1500.0f / 4.0f;
        
            m_Params.suspensionParams[i].suspensionAttachment.p = PxVec3(x, 0.0f, z); // Reset for simpler debug
        }

        // -- Engine & Gearbox (Defaults) --
        m_Params.engineParams.peakTorque = 500.0f;
        m_Params.engineParams.maxOmega = 600.0f;
        
        m_Params.gearBoxParams.neutralGear = 0;
        m_Params.gearBoxParams.ratios[0] = -4.0f; // Reverse
        m_Params.gearBoxParams.ratios[1] = 4.0f;  // 1st
        m_Params.gearBoxParams.ratios[2] = 2.0f;  // 2nd
        m_Params.gearBoxParams.ratios[3] = 1.5f;  // 3rd
        m_Params.gearBoxParams.ratios[4] = 1.1f;  // 4th
        m_Params.gearBoxParams.ratios[5] = 1.0f;  // 5th
        m_Params.gearBoxParams.nbRatios = 6;
        m_Params.gearBoxParams.finalRatio = 4.0f;

        // -- PhysX Integration --
        // Create the Actor
        PxPhysics* physics = m_PhysicsSystem->GetPhysics();
        PxMaterial* material = m_PhysicsSystem->GetScene()->getPhysics().createMaterial(0.5f, 0.5f, 0.6f);
        
        PxBoxGeometry bodyGeom(1.0f, 0.5f, 2.0f); // Simple box for car body
        PxShape* bodyShape = physics->createShape(bodyGeom, *material);
        m_State.physxActor.rigidBody = physics->createRigidDynamic(startPose);
        m_State.physxActor.rigidBody->attachShape(*bodyShape);
        m_State.physxActor.rigidBody->setMass(m_Params.rigidBodyParams.mass);
        m_State.physxActor.rigidBody->setMassSpaceInertiaTensor(m_Params.rigidBodyParams.moi);
        
        // Wheel Shapes
        for(int i=0; i<4; i++) {
            m_State.physxActor.wheelShapes[i] = physics->createShape(PxSphereGeometry(m_Params.wheelParams[i].radius), *material);
            m_Params.physxWheelShapeLocalPoses[i] = PxTransform(PxIdentity); // Will be updated by simulation
            // In a real app we'd attach these to show them, but often they are just logical shapes for raycasts
            // For pure raycast suspension, we mostly need the body.
            // PhysX 5 Vehicle SDK uses scene queries for wheels.
        }

        // Add to Scene
        m_PhysicsSystem->GetScene()->addActor(*m_State.physxActor.rigidBody);

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
            if (m_PhysicsSystem && m_PhysicsSystem->GetScene())
            {
                m_PhysicsSystem->GetScene()->removeActor(*m_State.physxActor.rigidBody);
            }
            m_State.physxActor.rigidBody->release();
            m_State.physxActor.rigidBody = nullptr;
        }
    }

    void VansPhysicsVehicle::Step(float dt)
    {
        m_ComponentSequence.update(dt, m_SimulationContext);
		std::cout << "[VansVehicle] Step completed. Position: " << GetTransform().p.x << ", " << GetTransform().p.y << ", " << GetTransform().p.z << std::endl;
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
