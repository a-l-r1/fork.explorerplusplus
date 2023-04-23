// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#include "stdafx.h"
#include "BaseDialog.h"
#include "Controls.h"
#include "CustomGripper.h"
#include "DpiCompatibility.h"
#include "Helper.h"
#include "WindowHelper.h"
#include <unordered_map>

namespace
{
std::unordered_map<HWND, BaseDialog *> g_windowMap;
}

BaseDialog::BaseDialog(HINSTANCE resourceInstance, int iResource, HWND hParent,
	DialogSizingType dialogSizingType) :
	m_resourceInstance(resourceInstance),
	m_iResource(iResource),
	m_hParent(hParent),
	m_dialogSizingType(dialogSizingType)
{
}

INT_PTR CALLBACK BaseDialogProcStub(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_INITDIALOG:
	{
		/* Store a mapping between window handles
		and objects. This must be done, as each
		dialog is managed by a separate object,
		but all window calls come through this
		function.
		Since two or more dialogs may be
		shown at once (as a dialog can be
		modeless), this function needs to be able
		to send the specified messages to the
		correct object.
		May also use thunks - see
		http://www.hackcraft.net/cpp/windowsThunk/ */
		g_windowMap.insert(std::unordered_map<HWND, BaseDialog *>::value_type(hDlg,
			reinterpret_cast<BaseDialog *>(lParam)));
	}
	break;
	}

	auto itr = g_windowMap.find(hDlg);

	if (itr != g_windowMap.end())
	{
		return itr->second->BaseDialogProc(hDlg, uMsg, wParam, lParam);
	}

	return 0;
}

INT_PTR CALLBACK BaseDialog::BaseDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_INITDIALOG:
	{
		m_hDlg = hDlg;

		AddDynamicControls();

		if (m_dialogSizingType != DialogSizingType::None)
		{
			RECT windowRect;
			GetWindowRect(m_hDlg, &windowRect);

			/* Assume that the current width and height of
			the dialog are the minimum width and height.
			Note that at this point, the dialog has NOT
			been initialized in any way, so it will not
			have had a chance to be resized yet. */
			m_iMinWidth = GetRectWidth(&windowRect);
			m_iMinHeight = GetRectHeight(&windowRect);

			RECT clientRect;
			GetClientRect(m_hDlg, &clientRect);

			const SIZE gripperSize = CustomGripper::GetDpiScaledSize(m_hDlg);

			HWND gripper = CreateWindow(CustomGripper::CLASS_NAME, L"",
				WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, clientRect.right - gripperSize.cx,
				clientRect.bottom - gripperSize.cy, gripperSize.cx, gripperSize.cy, m_hDlg,
				reinterpret_cast<HMENU>(static_cast<INT_PTR>(GetGripperControlId())),
				GetModuleHandle(nullptr), nullptr);

			std::vector<ResizableDialogControl> controls = GetResizableControls();
			controls.emplace_back(gripper, MovingType::Both, SizingType::None);
			m_resizableDialogHelper = std::make_unique<ResizableDialogHelper>(m_hDlg, controls);
		}

		auto &dpiCompat = DpiCompatibility::GetInstance();
		UINT dpi = dpiCompat.GetDpiForWindow(m_hDlg);
		int iconWidth = dpiCompat.GetSystemMetricsForDpi(SM_CXSMICON, dpi);
		int iconHeight = dpiCompat.GetSystemMetricsForDpi(SM_CYSMICON, dpi);
		m_icon = GetDialogIcon(iconWidth, iconHeight);

		if (m_icon)
		{
			SetClassLongPtr(m_hDlg, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(m_icon.get()));
		}

		m_tipWnd = CreateTooltipControl(m_hDlg, m_resourceInstance);

		OnInitDialogBase();
	}
	break;

	case WM_GETMINMAXINFO:
		if (m_dialogSizingType != DialogSizingType::None)
		{
			auto pmmi = reinterpret_cast<LPMINMAXINFO>(lParam);

			pmmi->ptMinTrackSize.x = m_iMinWidth;
			pmmi->ptMinTrackSize.y = m_iMinHeight;

			if (m_dialogSizingType == DialogSizingType::Horizontal)
			{
				pmmi->ptMaxTrackSize.y = m_iMinHeight;
			}

			if (m_dialogSizingType == DialogSizingType::Vertical)
			{
				pmmi->ptMaxTrackSize.x = m_iMinWidth;
			}

			return 0;
		}
		break;

	case WM_SIZE:
		if (m_dialogSizingType != DialogSizingType::None)
		{
			m_resizableDialogHelper->UpdateControls(LOWORD(lParam), HIWORD(lParam));
			return 0;
		}
		break;

	case WM_DESTROY:
	{
		if (m_showingModelessDialog && m_modelessDialogDestroyedObserver)
		{
			m_modelessDialogDestroyedObserver();
		}

		/* Within WM_DESTROY, all child windows
		still exist. */
		SaveState();
	}
	break;

	case WM_NCDESTROY:
		g_windowMap.erase(g_windowMap.find(hDlg));
		break;
	}

	return ForwardMessage(hDlg, uMsg, wParam, lParam);
}

void BaseDialog::AddDynamicControls()
{
}

wil::unique_hicon BaseDialog::GetDialogIcon(int iconWidth, int iconHeight) const
{
	UNREFERENCED_PARAMETER(iconWidth);
	UNREFERENCED_PARAMETER(iconHeight);

	return nullptr;
}

void BaseDialog::OnInitDialogBase()
{
}

INT_PTR BaseDialog::GetDefaultReturnValue(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(hwnd);
	UNREFERENCED_PARAMETER(uMsg);
	UNREFERENCED_PARAMETER(wParam);
	UNREFERENCED_PARAMETER(lParam);

	return 0;
}

HINSTANCE BaseDialog::GetResourceInstance() const
{
	return m_resourceInstance;
}

INT_PTR BaseDialog::ShowModalDialog()
{
	/* Explicitly disallow the creation of another
	dialog from this object while a modeless dialog
	is been shown. */
	if (m_showingModelessDialog)
	{
		return -1;
	}

	return DialogBoxParam(m_resourceInstance, MAKEINTRESOURCE(m_iResource), m_hParent,
		BaseDialogProcStub, reinterpret_cast<LPARAM>(this));
}

HWND BaseDialog::ShowModelessDialog(std::function<void()> dialogDestroyedObserver)
{
	if (m_showingModelessDialog)
	{
		return nullptr;
	}

	HWND dialog = CreateDialogParam(m_resourceInstance, MAKEINTRESOURCE(m_iResource), m_hParent,
		BaseDialogProcStub, reinterpret_cast<LPARAM>(this));

	if (!dialog)
	{
		return nullptr;
	}

	m_showingModelessDialog = true;
	m_modelessDialogDestroyedObserver = dialogDestroyedObserver;

	return dialog;
}

std::vector<ResizableDialogControl> BaseDialog::GetResizableControls()
{
	return {};
}

void BaseDialog::SaveState()
{
}
