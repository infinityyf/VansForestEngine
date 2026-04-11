#pragma once
#include <cstdint>
#include <vector>
#include <mutex>
#include <string>
#include <glm/glm.hpp>

namespace VansEngine
{
	// 碰撞事件类型
	enum class PhysicsEventType
	{
		CollisionEnter,
		CollisionExit,
		TriggerEnter,
		TriggerExit
	};

	// 碰撞事件数据（存储在主线程队列中供 Python 脚本消费）
	struct PhysicsEventData
	{
		PhysicsEventType type;

		// 涉及的两个物理体的 TransformID
		uint32_t transformID_A = 0;
		uint32_t transformID_B = 0;

		// 碰撞接触点信息 (仅 Collision 类型有效)
		glm::vec3 contactPoint  = glm::vec3(0.0f);
		glm::vec3 contactNormal = glm::vec3(0.0f);
		float     impulse       = 0.0f;

		// 被碰撞物体的名称（方便 Python 侧快速判断）
		std::string nameA;
		std::string nameB;
	};

	// 线程安全的事件队列
	class VansPhysicsEventQueue
	{
	public:
		void PushEvent(const PhysicsEventData& event)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_PendingEvents.push_back(event);
		}

		// 将所有待处理事件移动到输出数组，并清空内部队列
		void SwapEvents(std::vector<PhysicsEventData>& outEvents)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			outEvents.swap(m_PendingEvents);
			m_PendingEvents.clear();
		}

	private:
		std::mutex m_Mutex;
		std::vector<PhysicsEventData> m_PendingEvents;
	};
}
