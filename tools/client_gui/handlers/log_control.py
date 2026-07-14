"""
日志浏览控制处理器
"""

import os
import threading
import urllib.request
import flet as ft
from client_gui.handlers.base import BaseHandler


class LogControlHandler(BaseHandler):
    """日志浏览控制处理器"""

    def __init__(self, app):
        super().__init__(app)
        self._lock = threading.Lock()
        self._http_url = ""
        self._http_running = False

    def _log_page(self):
        ui = self.ui
        if ui and hasattr(ui, 'log_tab') and ui.log_tab:
            return ui.log_tab
        return None

    def _show_loading(self, message):
        page = self._log_page()
        if page and hasattr(page, 'loading_overlay') and page.loading_overlay:
            self.app.show_loading_overlay(page.loading_overlay, message)

    def _hide_loading(self):
        page = self._log_page()
        if page and hasattr(page, 'loading_overlay') and page.loading_overlay:
            self.app.hide_loading_overlay(page.loading_overlay)

    def refresh_log_list(self):
        page = self._log_page()
        if not page:
            return
        if not self.ble.connected:
            self.app.show_snack("请先连接设备")
            return

        def on_done(result):
            if isinstance(result, Exception) or result is None:
                self.app.show_snack("日志文件列表读取失败")
                return
            files = []
            for item in result:
                try:
                    if isinstance(item, dict):
                        filename = item.get("name", "")
                        size = item.get("size", 0)
                        mtime = item.get("mtime", 0)
                    else:
                        filename, size, mtime = item
                    files.append({
                        "filename": filename,
                        "size": size,
                        "mtime": mtime,
                    })
                except Exception:
                    continue
            files.sort(key=lambda x: x["mtime"], reverse=True)
            self._update_file_list_ui(files)

        self._run_with_loading(page.refresh_btn, self.ble.get_log_file_list(), on_done, "刷新中...")

    def _update_file_list_ui(self, files):
        page = self._log_page()
        if not page:
            return

        page.log_file_list.controls.clear()
        page.selected_file_name = None

        for f in files:
            file_item = self._create_file_item(f)
            page.log_file_list.controls.append(file_item)

        if not files:
            page.log_file_list.controls.append(
                ft.Container(
                    content=ft.Column([
                        ft.Icon(ft.Icons.FOLDER_OPEN, size=48, color=ft.Colors.GREY_400),
                        ft.Text("暂无日志文件", color=ft.Colors.GREY_600, size=14),
                    ], horizontal_alignment=ft.CrossAxisAlignment.CENTER, spacing=8),
                    alignment=ft.alignment.center,
                    padding=40,
                )
            )

        self.safe_update()

    def _create_file_item(self, file_info):
        filename = file_info["filename"]
        size = file_info["size"]
        mtime = file_info["mtime"]
        is_selected = (page.selected_file_name == filename) if (page := self._log_page()) else False

        preview_btn = ft.IconButton(
            icon=ft.Icons.VISIBILITY_OUTLINED,
            icon_size=18,
            tooltip="预览",
            on_click=lambda e, fn=filename: self.app.run_async(self._preview_file(fn)),
            padding=4,
        )

        download_btn = ft.IconButton(
            icon=ft.Icons.DOWNLOAD_OUTLINED,
            icon_size=18,
            tooltip="下载",
            on_click=lambda e, fn=filename: self.app.run_async(self._download_file(fn)),
            padding=4,
        )

        return ft.Container(
            content=ft.Row([
                ft.Icon(
                    ft.Icons.DESCRIPTION_OUTLINED,
                    size=20,
                    color=ft.Colors.BLUE_600 if is_selected else ft.Colors.GREY_500,
                ),
                ft.Column([
                    ft.Text(
                        filename,
                        size=13,
                        weight=ft.FontWeight.W_500 if is_selected else ft.FontWeight.W_400,
                        color=ft.Colors.BLUE_700 if is_selected else ft.Colors.ON_SURFACE,
                        overflow=ft.TextOverflow.ELLIPSIS,
                    ),
                    ft.Text(
                        f"{self._format_size(size)}  |  {self._format_time(mtime)}",
                        size=11,
                        color=ft.Colors.GREY_600,
                    ),
                ], spacing=2, expand=True),
                ft.Row([
                    preview_btn,
                    download_btn,
                ], spacing=2),
            ], spacing=8, alignment=ft.MainAxisAlignment.START),
            bgcolor=ft.Colors.BLUE_50 if is_selected else ft.Colors.TRANSPARENT,
            border=ft.border.all(
                1,
                ft.Colors.BLUE_300 if is_selected else ft.Colors.TRANSPARENT,
            ) if is_selected else None,
            padding=ft.Padding(left=8, right=8, top=6, bottom=6),
            border_radius=4,
            on_click=lambda e, fn=filename: self.app.run_async(self._select_file(fn)),
            ink=True,
        )

    def _format_size(self, size):
        if size >= 1024 * 1024:
            return f"{size / (1024 * 1024):.2f} MB"
        elif size >= 1024:
            return f"{size / 1024:.2f} KB"
        return f"{size} B"

    def _format_time(self, timestamp):
        from datetime import datetime
        try:
            return datetime.fromtimestamp(timestamp).strftime("%m-%d %H:%M")
        except Exception:
            return "未知"

    async def _select_file(self, filename):
        page = self._log_page()
        if not page:
            return
        page.selected_file_name = filename
        await self._preview_file(filename)

    async def _preview_file(self, filename):
        if not self.ble.connected:
            self.app.show_snack("请先连接设备")
            return
        page = self._log_page()
        if not page:
            return

        page.selected_file_name = filename
        self._show_loading(f"正在读取 {filename} ...")

        def on_selected(result):
            if isinstance(result, Exception) or not result:
                self._hide_loading()
                self.app.show_snack("无法选择日志文件")
                return
            def on_read(content):
                self._hide_loading()
                if isinstance(content, Exception):
                    self.app.show_snack(f"读取日志失败: {content}")
                    return
                self._show_content(filename, content)
            self.app.run_async(self.ble.read_log_file(), on_read)

        self.app.run_async(self.ble.select_log_file(filename), on_selected)

    def _show_content(self, filename, content):
        page = self._log_page()
        if not page:
            return

        if content is None:
            page.log_content_display.value = f"无法读取 {filename}"
        else:
            page.log_content_display.label = f"日志内容 - {filename}"
            page.log_content_display.value = content

        self.safe_update()

    async def _download_file(self, filename):
        if not self.ble.connected:
            self.app.show_snack("请先连接设备")
            return

        def _download_sync():
            try:
                home_dir = os.path.expanduser("~")
                downloads_dir = os.path.join(home_dir, "Downloads")
                if not os.path.exists(downloads_dir):
                    downloads_dir = home_dir
                save_path = os.path.join(downloads_dir, filename)

                if self._http_running and self._http_url:
                    file_url = self._http_url + "logs/" + filename
                    try:
                        self._show_loading(f"正在通过HTTP下载 {filename} ...")
                        urllib.request.urlretrieve(file_url, save_path)
                        self._hide_loading()
                        self.app.show_snack(f"已保存到: {save_path}")
                        self.app.run_async(self.ble.write_device_log(f"日志下载成功: {save_path}"))
                        return
                    except Exception as e:
                        self._hide_loading()
                        self.app.show_snack(f"HTTP下载失败，尝试BLE下载: {e}")

                def on_done(result):
                    if isinstance(result, Exception) or not result:
                        self.app.show_snack("下载失败，请启动HTTP服务器下载完整文件")
                    else:
                        self.app.show_snack(f"已保存到: {save_path}")
                        self.app.run_async(self.ble.write_device_log(f"日志下载成功: {save_path}"))

                self.app.run_async(self.ble.download_log_file(filename, save_path), on_done)
            except Exception as e:
                self._hide_loading()
                self.app.show_snack(f"下载失败: {e}")

        threading.Thread(target=_download_sync, daemon=True).start()

    async def toggle_http_server(self, enable):
        if not self.ble.connected:
            self.app.show_snack("请先连接设备")
            return
        page = self._log_page()
        if not page:
            return

        if enable:
            def on_start(result):
                if isinstance(result, Exception):
                    self._update_http_ui(False, "")
                    self.app.show_snack(f"HTTP服务器启动失败: {result}")
                    return
                def on_status(status):
                    if isinstance(status, Exception) or not status:
                        self._update_http_ui(False, "")
                        self.app.show_snack("HTTP服务器启动失败，请检查WiFi连接")
                        return
                    if status.get("running"):
                        url = status.get("url", "")
                        self._update_http_ui(True, url)
                        self.app.show_snack("HTTP服务器已启动")
                    else:
                        self._update_http_ui(False, "")
                        self.app.show_snack("HTTP服务器启动失败，请检查WiFi连接")
                self.app.run_async(self.ble.log_http_get_status(), on_status)
            self.app.run_async(self.ble.log_http_start(), on_start)
        else:
            def on_stop(result):
                self._update_http_ui(False, "")
                self.app.show_snack("HTTP服务器已停止")
            self.app.run_async(self.ble.log_http_stop(), on_stop)

    def _update_http_ui(self, running, url):
        page = self._log_page()
        self._http_running = running
        self._http_url = url if url else ""
        if page:
            page.update_http_status(running, url)
            self.safe_update()

    async def check_http_status(self):
        if not self.ble.connected:
            return
        page = self._log_page()
        if not page:
            return

        def on_status(status):
            if isinstance(status, Exception) or not status:
                return
            self._update_http_ui(status.get("running", False), status.get("url", ""))

        self.app.run_async(self.ble.log_http_get_status(), on_status)

    def on_tab_selected(self):
        self.refresh_log_list()
        self.app.run_async(self.check_http_status())
