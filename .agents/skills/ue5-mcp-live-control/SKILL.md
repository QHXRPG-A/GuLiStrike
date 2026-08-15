---
name: ue5-mcp-live-control
description: Control the live Unreal Engine 5 editor through the unrealMCP MCP server. Use whenever the user wants to inspect or modify the currently open UE5 project in real time — spawn/move/delete actors, build structures (houses, towns, castles, mazes), create and edit Blueprints, wire graph nodes, manage variables/functions, or apply materials. Also use when UE5 editor tasks mention "live", "current level", "editor is open", or when other ue5-* skills recommend dedicated MCP tools.
---

# UE5 Live Editor Control via unrealMCP

Operate the **currently running** UE5 editor (the workspace is a UE5 project, e.g. `test1.uproject`) through the `unrealMCP` MCP server. Tools are exposed as `mcp__unrealMCP__<tool_name>`.

## Prerequisites

- The UE5 editor must be running with the UnrealMCP plugin loaded; the plugin listens on `127.0.0.1:55557`.
- If tools time out or return connection errors, the editor is closed or the plugin is not loaded — tell the user to open the project in the editor before retrying. Do not retry in a loop.

## Workflow

1. Orient first: call `mcp__unrealMCP__get_actors_in_level` or `mcp__unrealMCP__find_actors_by_name` before modifying anything, so edits target actors that actually exist.
2. Pick the most specific tool instead of stacking many small ones — e.g. one `construct_house` call beats dozens of spawn/transform calls.
3. After structural or Blueprint edits, verify: re-read actors or `read_blueprint_content` to confirm the change landed, and `compile_blueprint` after graph edits.
4. For asset creation beyond live control (C++ modules, UMG design patterns, PCG, packaging), route to the matching `ue5-*` skill; use MCP tools only for the live-editor portion.

## Tool map

Full catalog with arguments: read `references/tool-catalog.md`. Summary:

- **Level/actors**: `get_actors_in_level`, `find_actors_by_name`, `delete_actor`, `set_actor_transform`
- **Structures (one-call world building)**: `create_town`, `construct_house`, `construct_mansion`, `create_castle_fortress`, `create_tower`, `create_wall`, `create_arch`, `create_staircase`, `create_pyramid`, `create_maze`, `create_suspension_bridge`, `create_aqueduct`
- **Blueprint assets**: `create_blueprint`, `compile_blueprint`, `add_component_to_blueprint`, `set_static_mesh_properties`, `spawn_physics_blueprint_actor`
- **Blueprint graphs**: `add_node`, `add_event_node`, `connect_nodes`, `delete_node`, `set_node_property`
- **Blueprint variables/functions**: `create_variable`, `set_blueprint_variable_properties`, `get_blueprint_variable_details`, `create_function`, `add_function_input`, `add_function_output`, `delete_function`, `rename_function`
- **Blueprint analysis**: `read_blueprint_content`, `analyze_blueprint_graph`, `get_blueprint_function_details`
- **Materials/physics**: `get_available_materials`, `apply_material_to_actor`, `apply_material_to_blueprint`, `get_actor_material_info`, `set_mesh_material_color`, `set_physics_properties`

## Notes

- Actor/Blueprint names are matched by name string; prefer `find_actors_by_name` to get the exact name before `set_actor_transform` / `delete_actor`.
- Tools return JSON with a `status` field — check for `"success"` and surface failures to the user instead of silently continuing.
- Coordinate units are centimeters; transforms are `[x, y, z]` arrays.
