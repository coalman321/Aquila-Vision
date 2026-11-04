"""ros2cli commands and verbs enabled in //tools/ros2, and their entry_points.txt."""

_VERBS = {
    "action": {
        "command": "ActionCommand",
        "verbs": {
            "echo": "EchoVerb",
            "find": "FindVerb",
            "info": "InfoVerb",
            "list": "ListVerb",
            "send_goal": "SendGoalVerb",
            "type": "TypeVerb",
        },
    },
    "node": {
        "command": "NodeCommand",
        "verbs": {
            "info": "InfoVerb",
            "list": "ListVerb",
        },
    },
    "param": {
        "command": "ParamCommand",
        "verbs": {
            "delete": "DeleteVerb",
            "describe": "DescribeVerb",
            "dump": "DumpVerb",
            "get": "GetVerb",
            "list": "ListVerb",
            "load": "LoadVerb",
            "set": "SetVerb",
        },
    },
    "pkg": {
        "command": "PkgCommand",
        "verbs": {
            "create": "CreateVerb",
            "executables": "ExecutablesVerb",
            "list": "ListVerb",
            "prefix": "PrefixVerb",
            "xml": "XmlVerb",
        },
    },
    "service": {
        "command": "ServiceCommand",
        "verbs": {
            "call": "CallVerb",
            "echo": "EchoVerb",
            "find": "FindVerb",
            "info": "InfoVerb",
            "list": "ListVerb",
            "type": "TypeVerb",
        },
    },
    "topic": {
        "command": "TopicCommand",
        "verbs": {
            "bw": "BwVerb",
            "delay": "DelayVerb",
            "echo": "EchoVerb",
            "find": "FindVerb",
            "hz": "HzVerb",
            "info": "InfoVerb",
            "list": "ListVerb",
            "pub": "PubVerb",
            "type": "TypeVerb",
        },
    },
}

def entry_points():
    """Returns the lines of an entry_points.txt registering every verb in _VERBS."""
    commands = []
    extension_points = []
    verb_groups = []
    for name, spec in sorted(_VERBS.items()):
        pkg = "ros2" + name
        commands.append("{0} = {1}.command.{0}:{2}".format(name, pkg, spec["command"]))
        extension_points.append("{0}.verb = {0}.verb:VerbExtension".format(pkg))
        verb_groups.append("")
        verb_groups.append("[{}.verb]".format(pkg))
        for verb, cls in sorted(spec["verbs"].items()):
            verb_groups.append("{0} = {1}.verb.{0}:{2}".format(verb, pkg, cls))
    return (
        ["[ros2cli.command]"] + commands +
        ["", "[ros2cli.extension_point]"] + extension_points +
        verb_groups
    )
