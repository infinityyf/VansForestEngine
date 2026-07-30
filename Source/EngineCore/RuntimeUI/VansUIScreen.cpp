#include "Public/VansUIScreen.h"

#include "Public/VansUIElementHandle.h"
#include "Public/VansUIViewModel.h"

#include <utility>

namespace VansRuntime
{
	VansUIScreen::VansUIScreen(
		VansUIHandleId handle,
		VansUIScreenConfig config,
		std::shared_ptr<VansUIDocument> document,
		std::shared_ptr<VansUIViewModel> viewModel)
		: m_Handle(handle)
		, m_Config(std::move(config))
		, m_Document(std::move(document))
		, m_ViewModel(std::move(viewModel))
	{
		if (m_Document && m_ViewModel)
			m_Document->SetDataContext(m_ViewModel.get());
	}

	void VansUIScreen::Show()
	{
		if (m_Document)
			m_Document->Show();
	}

	void VansUIScreen::Hide()
	{
		if (m_Document)
			m_Document->Hide();
	}

	void VansUIScreen::Close()
	{
		m_Open = false;
		if (m_Document)
			m_Document->Hide();
	}

	void VansUIScreen::SetViewModel(std::shared_ptr<VansUIViewModel> viewModel)
	{
		m_ViewModel = std::move(viewModel);
		if (m_Document)
			m_Document->SetDataContext(m_ViewModel.get());
	}

	bool VansUIScreen::IsVisible() const
	{
		return m_Document && m_Document->IsVisible();
	}

	VansUIElementHandle VansUIScreen::FindElement(const std::string& name)
	{
		return m_Document ? m_Document->FindElement(name) : VansUIElementHandle{};
	}
}
