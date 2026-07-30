#pragma once

#include "VansUIDocument.h"
#include "VansUIRuntimeHandles.h"
#include "VansUIScreenConfig.h"

#include <memory>
#include <string>
#include <utility>

namespace VansRuntime
{
	class VansUIViewModel;

	class VansUIScreen
	{
	public:
		VansUIScreen(
			VansUIHandleId handle,
			VansUIScreenConfig config,
			std::shared_ptr<VansUIDocument> document,
			std::shared_ptr<VansUIViewModel> viewModel = nullptr);

		VansUIHandleId GetHandleId() const { return m_Handle; }
		const std::string& GetGuid() const { return m_Config.guid; }
		const std::string& GetName() const { return m_Config.name; }
		const VansUIScreenConfig& GetConfig() const { return m_Config; }
		std::shared_ptr<VansUIDocument> GetDocument() const { return m_Document; }
		VansUIDocument* GetDocumentRaw() const { return m_Document.get(); }
		std::shared_ptr<VansUIViewModel> GetViewModel() const { return m_ViewModel; }

		void Show();
		void Hide();
		void Close();
		void SetViewModel(std::shared_ptr<VansUIViewModel> viewModel);
		bool IsOpen() const { return m_Open && m_Document != nullptr; }
		bool IsVisible() const;
		VansUIElementHandle FindElement(const std::string& name);

	private:
		VansUIHandleId m_Handle = kInvalidUIHandle;
		VansUIScreenConfig m_Config;
		std::shared_ptr<VansUIDocument> m_Document;
		std::shared_ptr<VansUIViewModel> m_ViewModel;
		bool m_Open = true;
	};
}
