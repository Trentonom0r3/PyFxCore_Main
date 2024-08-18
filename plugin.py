from abc import ABC, abstractmethod

class Command(ABC):
    def __init__(self, name, menu_id, after_item=None):
        self.name = name
        self.menu_id = menu_id
        self.after_item = after_item

    @abstractmethod
    def execute(self):
        pass

    @abstractmethod
    def update_menu(self):
        pass

class Plugin(ABC):
    instance = None

    def __init__(self):
        Plugin.instance = self
        self.commands = []

    @abstractmethod
    def on_init(self):
        pass

    @abstractmethod
    def on_death(self):
        pass

    @abstractmethod
    def on_idle(self):
        pass

    def add_command(self, command):
        self.commands.append(command)

    def command_hook(self, command_id):
        for command in self.commands:
            if command.name == command_id:
                command.execute()
                return

    def update_menu_hook(self):
        for command in self.commands:
            command.update_menu()

    def death_hook(self):
        self.on_death()

    def idle_hook(self):
        self.on_idle()

# Example usage by a user
class MyCommand(Command):
    def execute(self):
        print("Executing MyCommand")

    def update_menu(self):
        print("Updating menu for MyCommand")

class MyPlugin(Plugin):
    def on_init(self):
        print("Initializing MyPlugin")
        self.add_command(MyCommand(name="MyCommand", menu_id="MenuID"))

    def on_death(self):
        print("Plugin is shutting down")

    def on_idle(self):
        print("Plugin is idle")

# Instantiating the plugin
plugin = MyPlugin()
plugin.on_init()
plugin.command_hook("MyCommand")
plugin.update_menu_hook()
plugin.idle_hook()
plugin.death_hook()
