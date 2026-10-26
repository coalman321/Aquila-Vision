import os.path
from typing import Dict, List

from ament_index_python import get_package_share_directory
from process_msgs.msg import ProcessInfo
from process_msgs.srv import RemoteRequest
import rclpy
from textual import work
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal
from textual.widgets import (
    DataTable,
    Footer,
    Header,
    Input,
    Label,
    Markdown,
)
from textual.widgets.data_table import RowDoesNotExist
from textual.worker import get_current_worker

from .launch_tui_node import LaunchTuiNode

STAT_TABLE = [
    'Inactive', 'Starting', 'Running', 'Timeout',
    'Stopping', 'Restarting', 'Dead', 'Invalid',
]


class LaunchApp(App):
    CSS_PATH = os.path.join(
        get_package_share_directory('launch_tui'), 'css', 'layout.tcss'
    )

    BINDINGS = [
        Binding('s', action='start_launch', description='Start launch'),
        Binding('k', action='stop_launch', description='Kill launch'),
        Binding('c', action='configure_launch', description='Configure launch'),
        Binding('v', action='start_process', description='Start process'),
        Binding('h', action='stop_process', description='Halt process'),
        Binding('r', action='restart_process', description='Restart process'),
    ]

    def get_arg_input_element(self) -> Input:
        footer = self.get_child_by_id('control_footer')
        input_section = footer.get_child_by_id('control_input')
        request_input = input_section.get_child_by_id('request_input')
        return request_input

    def get_selected_row_info(self):
        """Get system and process ID from currently highlighted row."""
        process_table = self.get_child_by_id('launch_items')

        if process_table.cursor_row < 0 or process_table.cursor_row >= len(process_table.rows):
            return None, None

        # Get the row key from the ordered dict of rows
        row_keys = list(process_table.rows.keys())

        if process_table.cursor_row >= len(row_keys):
            return None, None

        row_key_obj = row_keys[process_table.cursor_row]
        # RowKey objects have a .value attribute that contains the actual key string
        row_key = row_key_obj.value if hasattr(row_key_obj, 'value') else str(row_key_obj)

        # Extract system name and process ID from row key (format: "system:process_id")
        if ':' in str(row_key):
            parts = str(row_key).split(':')
            system_name = parts[0]
            try:
                process_id = int(parts[1])
                return system_name, process_id
            except (ValueError, IndexError):
                return None, None

        return None, None

    async def action_start_launch(self) -> None:
        system_name, _ = self.get_selected_row_info()
        if system_name is None:
            self.notify('No system selected. Select a row first.', severity='error')
            return
        self.node.send_launch_request(system_name)

    async def action_stop_launch(self) -> None:
        system_name, _ = self.get_selected_row_info()
        if system_name is None:
            self.notify('No system selected. Select a row first.', severity='error')
            return
        self.node.send_shutdown_request(system_name)

    def action_configure_launch(self) -> None:
        system_name, _ = self.get_selected_row_info()
        if system_name is None:
            self.notify('No system selected. Select a row first.', severity='error')
            return
        self.selected_system = system_name
        self.request_type = 'configure_launch'
        self.get_arg_input_element().focus()

    def action_start_process(self) -> None:
        system_name, process_id = self.get_selected_row_info()
        if system_name is None or process_id is None:
            self.notify('No process selected. Select a row first.', severity='error')
            return
        self.node.send_start_request(system_name, process_id)
        self.notify(f'Starting process {process_id} on {system_name}', severity='information')

    def action_stop_process(self) -> None:
        system_name, process_id = self.get_selected_row_info()
        if system_name is None or process_id is None:
            self.notify('No process selected. Select a row first.', severity='error')
            return
        self.node.send_stop_request(system_name, process_id)
        self.notify(f'Stopping process {process_id} on {system_name}', severity='information')

    def action_restart_process(self) -> None:
        system_name, process_id = self.get_selected_row_info()
        if system_name is None or process_id is None:
            self.notify('No process selected. Select a row first.', severity='error')
            return
        self.node.send_restart_request(system_name, process_id)
        self.notify(f'Restarting process {process_id} on {system_name}', severity='information')

    def compose(self) -> ComposeResult:
        yield Header('Launch TUI')

        table = DataTable(id='launch_items', classes='launch_table', cursor_type='row')
        table.add_column('System', key='system')
        table.add_column('ID', key='id')
        table.add_column('Name', key='name')
        table.add_column('Status', key='stat')
        table.add_column('Restarts', key='restarts')
        table.add_column('PID', key='pid')
        table.add_column('% CPU', key='cpu')
        table.add_column('Memory (MB)', key='mem')
        yield table

        yield Horizontal(
            Markdown(
                'Launch config: ',
                id='launch_config_label',
                classes='control_footer_item',
            ),
            Horizontal(
                Label('Command: ', id='launch_command_label', classes='cmd_label'),
                Input(id='request_input', classes='cmd_input'),
                classes='control_footer_item',
                id='control_input',
            ),
            classes='control_footer',
            id='control_footer',
        )

        yield Footer()

    async def on_mount(self) -> None:
        # init the ros node
        self.node = LaunchTuiNode()
        self.request_type = None
        self.selected_system = None  # Track currently selected system
        self.selected_process_id = None  # Track currently selected process ID

        # start the ros thread
        self.update_ros()

    async def on_input_submitted(self, msg):
        if self.request_type is not None:
            if self.selected_system is None:
                self.notify('No system selected. Select a row first.', severity='error')
                self.get_arg_input_element().clear()
                self.request_type = None
                return

            if self.request_type == 'configure_launch':
                self.node.send_config_request(self.selected_system, str(msg.value))
            else:
                self.panic('Unknown request type')

            self.get_arg_input_element().clear()
            self.request_type = None
        else:
            await self.action_bell()

        # refocus the table
        process_table = self.get_child_by_id('launch_items')
        process_table.focus()

    async def on_new_process_list(self, system_processes: Dict[str, List[ProcessInfo]]) -> None:
        process_table: DataTable
        process_table = self.get_child_by_id('launch_items')

        # Collect all processes from all systems
        all_processes = []
        for system_name, processes in system_processes.items():
            for proc in processes:
                all_processes.append((system_name, proc))

        if len(process_table.rows) > len(all_processes):
            process_table.clear()

        # Register the new items
        for system_name, info in all_processes:
            # Create unique row key combining system and process ID
            row_key = f'{system_name}:{info.process_id}'

            # Test if the row exists
            try:
                process_table.get_row(row_key)
            except RowDoesNotExist:
                # If it doesn't exist, create it
                process_table.add_row(
                    system_name,
                    info.process_id,
                    info.process_name,
                    key=row_key,
                )

            cpu_pct = round(info.process_cpu_pct, 3) if info.process_cpu_pct > 0.0 else 0.0

            # Update the existing entry
            process_table.update_cell(
                row_key,
                'stat',
                STAT_TABLE[info.process_status],
            )
            process_table.update_cell(row_key, 'pid', info.local_pid, update_width=True)
            process_table.update_cell(row_key, 'restarts', info.restart_counter, update_width=True)
            process_table.update_cell(row_key, 'cpu', str(round(cpu_pct * 100.0, 4)))
            process_table.update_cell(row_key, 'mem', str(round(info.process_memory * 1e-6, 4)))

    async def on_launch_config_name(self, system_configs: Dict[str, str]) -> None:
        footer = self.get_child_by_id('control_footer')
        config_text: Markdown
        config_text = footer.get_child_by_id('launch_config_label')

        # Display all system configs
        config_lines = []
        for system_name, config_name in system_configs.items():
            config_lines.append(f'**{system_name}**: {config_name}')

        if config_lines:
            config_text.update('Launch Configs:\n' + '\n'.join(config_lines))
        else:
            config_text.update('Launch Config: (none)')

    async def on_data_table_row_selected(self, event) -> None:
        """Handle row selection to track which system and process are selected."""
        row_key = event.row_key

        # Extract system name and process ID from row key (format: "system:process_id")
        if ':' in str(row_key):
            parts = str(row_key).split(':')
            self.selected_system = parts[0]
            try:
                self.selected_process_id = int(parts[1])
            except (ValueError, IndexError):
                self.selected_process_id = None
        else:
            self.selected_system = None
            self.selected_process_id = None

    @work(exclusive=True, thread=True)
    def update_ros(self) -> None:

        # keep it spinning while the worker is alive
        while not get_current_worker().is_cancelled:
            rclpy.spin_once(self.node)

            # update the process list
            if (not get_current_worker().is_cancelled) and self.node.has_new_data():
                proc_dict = self.node.get_process_list()
                config_dict = self.node.get_system_configs()

                try:
                    self.call_from_thread(self.on_new_process_list, proc_dict)
                    self.call_from_thread(self.on_launch_config_name, config_dict)
                except Exception:
                    pass

            # buffer out any command results
            if self.node.handle_requests():
                result = self.node.get_request_result()
                level = (
                    'information'
                    if result.resp_code == RemoteRequest.Response.RESPONSE_OK
                    else 'error'
                )
                self.notify(result.response_msg, title='Response:', severity=level)
