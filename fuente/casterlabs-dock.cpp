// SPDX-License-Identifier: GPL-2.0-or-later
#if defined(__clang__) && defined(_MSC_VER)
// OBS's inline utility selects an MSVC-only intrinsic which Clang lacks.
#include <cstdint>
#pragma push_macro("_MSC_VER")
#undef _MSC_VER
#include <util/util_uint64.h>
#pragma pop_macro("_MSC_VER")
#endif
#include <obs-module.h>
#include <obs-frontend-api.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <QDesktopServices>
#include <QDockWidget>
#include <QFileInfo>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QShowEvent>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

OBS_DECLARE_MODULE()

namespace {
constexpr auto dockId = "casterlabs-caffeinated-dock";
constexpr auto executable = L"Casterlabs-Caffeinated.exe";

DWORD windowProcess(HWND window)
{
	DWORD process = 0;
	GetWindowThreadProcessId(window, &process);
	return process;
}

bool isCasterlabsProcess(DWORD processId)
{
	const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
	if (!process)
		return false;
	wchar_t path[32768];
	DWORD length = ARRAYSIZE(path);
	const bool queried = QueryFullProcessImageNameW(process, 0, path, &length);
	CloseHandle(process);
	if (!queried)
		return false;
	const QString name = QFileInfo(QString::fromWCharArray(path, length)).fileName();
	return name.compare(QString::fromWCharArray(executable), Qt::CaseInsensitive) == 0;
}

HWND findCasterlabsWindow()
{
	HWND result = nullptr;
	EnumWindows([](HWND window, LPARAM context) -> BOOL {
		if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) || IsHungAppWindow(window))
			return TRUE;
		wchar_t title[256];
		if (!GetWindowTextW(window, title, ARRAYSIZE(title)))
			return TRUE;
		const QString name = QString::fromWCharArray(title);
		if (name.contains(QStringLiteral("Audio Player"), Qt::CaseInsensitive) ||
		    (!name.contains(QStringLiteral("Caffeinated"), Qt::CaseInsensitive) &&
		     !name.contains(QStringLiteral("Casterlabs"), Qt::CaseInsensitive)))
			return TRUE;
		if (!isCasterlabsProcess(windowProcess(window)))
			return TRUE;
		*reinterpret_cast<HWND *>(context) = window;
		return FALSE;
	}, reinterpret_cast<LPARAM>(&result));
	return result;
}

bool setNativeStyle(HWND window, int index, LONG_PTR value)
{
	SetLastError(ERROR_SUCCESS);
	return SetWindowLongPtrW(window, index, value) != 0 || GetLastError() == ERROR_SUCCESS;
}

bool setNativeParent(HWND window, HWND parent)
{
	SetLastError(ERROR_SUCCESS);
	return SetParent(window, parent) != nullptr || GetLastError() == ERROR_SUCCESS;
}

class CasterlabsPanel final : public QWidget {
public:
	CasterlabsPanel()
	{
		auto *layout = new QVBoxLayout(this);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(4);
		status_ = new QLabel(tr("Abre Casterlabs para acoplar su ventana."), this);
		status_->setWordWrap(true);
		button_ = new QPushButton(tr("Abrir o acoplar Casterlabs"), this);
		host_ = new QWidget(this);
		host_->setAttribute(Qt::WA_DontCreateNativeAncestors);
		host_->setAttribute(Qt::WA_NativeWindow);
		host_->setMinimumSize(400, 300);
		host_->installEventFilter(this);
		layout->addWidget(status_);
		layout->addWidget(button_);
		layout->addWidget(host_, 1);

		timer_.setInterval(500);
		connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
		connect(button_, &QPushButton::clicked, this, [this] {
			if (window_)
				detach();
			else
				openOrAttach();
		});
	}

	~CasterlabsPanel() override { detach(); }
	QSize sizeHint() const override { return {540, 580}; }

	void detach()
	{
		attempts_ = 0;
		timer_.stop();
		if (ownsWindow()) {
			const bool visible = GetWindowLongPtrW(window_, GWL_STYLE) & WS_VISIBLE;
			if (GetParent(window_) == hostHandle() && !setNativeParent(window_, nullptr)) {
				blog(LOG_ERROR, "[casterlabs-dock] Could not release the Casterlabs window (%lu)", GetLastError());
				return;
			}
			setNativeStyle(window_, GWL_STYLE, visible ? style_ : style_ & ~WS_VISIBLE);
			setNativeStyle(window_, GWL_EXSTYLE, extendedStyle_);
			SetWindowPos(window_, nullptr, bounds_.left, bounds_.top, bounds_.right - bounds_.left,
				     bounds_.bottom - bounds_.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
			auto placement = placement_;
			if (!visible)
				placement.showCmd = SW_HIDE;
			SetWindowPlacement(window_, &placement);
			blog(LOG_INFO, "[casterlabs-dock] Restored the Casterlabs window");
		}
		window_ = nullptr;
		status_->setText(tr("Abre Casterlabs para acoplar su ventana."));
		status_->show();
		button_->setText(tr("Abrir o acoplar Casterlabs"));
	}

protected:
	void showEvent(QShowEvent *event) override
	{
		QWidget::showEvent(event);
		QTimer::singleShot(0, this, [this] {
			if (isVisible() && !window_ && !timer_.isActive())
				attach(findCasterlabsWindow());
		});
	}

	bool event(QEvent *event) override
	{
		if (static_cast<int>(event->type()) == QEvent::User + QEvent::Close)
			detach();
		return QWidget::event(event);
	}

	bool eventFilter(QObject *object, QEvent *event) override
	{
		if (object == host_ && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
			fitWindow();
		return QWidget::eventFilter(object, event);
	}

private:
	HWND hostHandle() const { return reinterpret_cast<HWND>(host_->internalWinId()); }

	bool ownsWindow() const { return window_ && windowProcess(window_) == processId_; }

	void fitWindow()
	{
		const HWND host = hostHandle();
		RECT client{};
		if (host && ownsWindow() && GetParent(window_) == host && GetClientRect(host, &client)) {
			SetWindowPos(window_, nullptr, 0, 0, client.right, client.bottom,
				     SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
		}
	}

	void attach(HWND candidate)
	{
		if (!candidate)
			return;
		timer_.stop();
		attempts_ = 0;

		const auto host = reinterpret_cast<HWND>(host_->winId());
		placement_ = {};
		placement_.length = sizeof(placement_);
		if (!GetWindowPlacement(candidate, &placement_))
			return;
		if (IsIconic(candidate) || IsZoomed(candidate))
			ShowWindow(candidate, SW_RESTORE);
		if (!GetWindowRect(candidate, &bounds_))
			return;
		style_ = GetWindowLongPtrW(candidate, GWL_STYLE);
		extendedStyle_ = GetWindowLongPtrW(candidate, GWL_EXSTYLE);
		const LONG_PTR childStyle = (style_ & ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU |
							WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_CHILD;
		const LONG_PTR childExtendedStyle = extendedStyle_ & ~(WS_EX_APPWINDOW | WS_EX_WINDOWEDGE);
		if (!setNativeStyle(candidate, GWL_STYLE, childStyle) ||
		    !setNativeStyle(candidate, GWL_EXSTYLE, childExtendedStyle) || !setNativeParent(candidate, host)) {
			const DWORD error = GetLastError();
			setNativeStyle(candidate, GWL_STYLE, style_);
			setNativeStyle(candidate, GWL_EXSTYLE, extendedStyle_);
			SetWindowPlacement(candidate, &placement_);
			status_->setText(tr("Windows no pudo acoplar la ventana. Abre ambas apps con los mismos permisos."));
			blog(LOG_WARNING, "[casterlabs-dock] Could not attach the Casterlabs window (%lu)", error);
			return;
		}

		window_ = candidate;
		processId_ = windowProcess(candidate);
		status_->hide();
		button_->setText(tr("Desacoplar Casterlabs"));
		SetWindowPos(window_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
							SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS);
		fitWindow();
		timer_.start();
		blog(LOG_INFO, "[casterlabs-dock] Attached the Casterlabs window");
	}

	void openOrAttach()
	{
		if (const HWND candidate = findCasterlabsWindow()) {
			attach(candidate);
			return;
		}
		const QString shortcut = QStandardPaths::locate(QStandardPaths::ApplicationsLocation,
			QStringLiteral("Casterlabs/Casterlabs Caffeinated.lnk"));
		const bool launched = !shortcut.isEmpty() && QDesktopServices::openUrl(QUrl::fromLocalFile(shortcut));
		status_->setText(launched ? tr("Esperando la ventana de Casterlabs. Si está en la bandeja, ábrela desde ahí.")
					 : tr("Abre Casterlabs desde Inicio o desde la bandeja de Windows."));
		attempts_ = 50;
		timer_.start();
	}

	void tick()
	{
		if (window_) {
			if (!ownsWindow() || GetParent(window_) != hostHandle() ||
			    !(GetWindowLongPtrW(window_, GWL_STYLE) & WS_VISIBLE))
				detach();
			return;
		}
		if (!isVisible() || attempts_ == 0) {
			timer_.stop();
			status_->setText(tr("Abre la ventana principal de Casterlabs y vuelve a pulsar el botón."));
			return;
		}
		--attempts_;
		attach(findCasterlabsWindow());
	}

	QLabel *status_ = nullptr;
	QPushButton *button_ = nullptr;
	QWidget *host_ = nullptr;
	QTimer timer_;
	HWND window_ = nullptr;
	DWORD processId_ = 0;
	LONG_PTR style_ = 0;
	LONG_PTR extendedStyle_ = 0;
	RECT bounds_{};
	WINDOWPLACEMENT placement_{};
	int attempts_ = 0;
};

QPointer<CasterlabsPanel> panel;

void frontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_EXIT && panel)
		panel->detach();
}
} // namespace

MODULE_EXPORT const char *obs_module_name(void) { return "Casterlabs Dock"; }

bool obs_module_load(void)
{
	auto *widget = new CasterlabsPanel;
	if (!obs_frontend_add_dock_by_id(dockId, "Casterlabs Caffeinated", widget)) {
		delete widget;
		return false;
	}
	panel = widget;
	if (auto *dock = qobject_cast<QDockWidget *>(widget->parentWidget()))
		dock->setFloating(false);
	obs_frontend_add_event_callback(frontendEvent, nullptr);
	blog(LOG_INFO, "[casterlabs-dock] Registered the Casterlabs Caffeinated dock");
	return true;
}
