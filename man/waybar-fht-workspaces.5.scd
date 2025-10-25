waybar-fht-workspaces(5)

# NAME

waybar - fht workspaces module

# DESCRIPTION

The *workspaces* module displays the currently used workspaces in fht-compositor.

# CONFIGURATION

Addressed by *fht/workspaces*

*all-outputs*: ++
	typeof: bool ++
	default: false ++
	If set to false, workspaces will only be shown on the output they are on. If set to true all workspaces will be shown on every output.

*format*: ++
	typeof: string ++
	default: {value} ++
	The format, how information should be displayed.

*format-icons*: ++
	typeof: array ++
	Based on the workspace id and state, the corresponding icon gets selected. See *icons*.

*disable-click*: ++
	typeof: bool ++
	default: false ++
	If set to false, you can click to change workspace. If set to true this behaviour is disabled.

*disable-markup*: ++
	typeof: bool ++
	default: false ++
	If set to true, button label will escape pango markup.

*current-only*: ++
	typeof: bool ++
	default: false ++
	If set to true, only the active or focused workspace will be shown.

*on-update*: ++
	typeof: string ++
	Command to execute when the module is updated.

*expand*: ++
	typeof: bool ++
	default: false ++
	Enables this module to consume all left over space dynamically.

# FORMAT REPLACEMENTS

*{value}*: ID of the workspace.

*{icon}*: Icon, as defined in *format-icons*.

*{id}*: ID of the workspace.

*{output}*: Output where the workspace is located.

# ICONS

Additional to workspace id matching, the following *format-icons* can be set.

- *default*: Will be shown, when no string matches are found.
- *focused*: Will be shown, when workspace is focused.
- *active*: Will be shown, when workspace is active on its output.
- *empty*: Will be shown, when workspace is empty.

# EXAMPLES

```
"fht/workspaces": {
	"format": "{icon}",
	"format-icons": {
		// Icons by workspace id
		"1": "",
		"2": "",
		"3": "",

		// Icons by state
		"active": "",
		"default": ""
	}
}
```

# Style

- *#workspaces button*
- *#workspaces button.focused*: The single focused workspace.
- *#workspaces button.active*: The workspace is active (visible) on its output.
- *#workspaces button.empty*: The workspace is empty.
- *#workspaces button.current_output*: The workspace is from the same output as
  the bar that it is displayed on.
- *#workspaces button#fht-workspace-<id>*: Workspace with this id.
