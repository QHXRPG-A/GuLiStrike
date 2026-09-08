# unrealMCP Tool Catalog

Server: `unrealMCP` (UnrealMCP_Advanced v1.5.0, local stdio server at `D:\UE5.7\test1\unreal-engine-mcp\Python\unreal_mcp_server_advanced.py`, talks to the UnrealMCP editor plugin over TCP `127.0.0.1:55557`).

All tools are called as `mcp__unrealMCP__<name>`. Responses are JSON with a `status` field.

## Level Actors

| Tool | Key arguments | Notes |
|---|---|---|
| `get_actors_in_level` | — | Lists every actor: name, class, location, rotation, scale. Orientation first. |
| `find_actors_by_name` | `pattern` | Substring match on actor name. Use to get exact names. |
| `delete_actor` | `name` | Exact actor name. |
| `set_actor_transform` | `name`, `location?`, `rotation?`, `scale?` | Each transform is a `[x, y, z]` array (centimeters / degrees). |

## Structures / World Building

| Tool | Key arguments | Notes |
|---|---|---|
| `create_town` | varies | Generates a settlement of houses. |
| `construct_house` | varies | Single house. |
| `construct_mansion` | varies | Larger residence. |
| `create_castle_fortress` | varies | Fortress complex. |
| `create_tower` | varies | Tower. |
| `create_wall` | varies | Wall segment. |
| `create_arch` | varies | Arch. |
| `create_staircase` | varies | Staircase. |
| `create_pyramid` | varies | Pyramid. |
| `create_maze` | varies | Maze. |
| `create_suspension_bridge` | varies | Suspension bridge. |
| `create_aqueduct` | varies | Aqueduct. |
| `spawn_physics_blueprint_actor` | varies | Spawns a physics-enabled BP actor into the level. |

Prefer one structure call over many manual spawns. Check `get_actors_in_level` afterward to verify placement.

## Blueprint Assets

| Tool | Key arguments | Notes |
|---|---|---|
| `create_blueprint` | `name`, `parent_class` | Creates a new Blueprint asset. |
| `add_component_to_blueprint` | `blueprint_name`, `component_type`, `component_name`, `location?`, `rotation?`, `scale?`, `component_properties?` | Adds a component, optionally sets transform/properties. |
| `set_static_mesh_properties` | `blueprint_name`, `component_name`, `static_mesh?` | Mesh asset path, default `/Engine/BasicShapes/Cube.Cube`. |
| `compile_blueprint` | `blueprint_name` | Compile after any graph/variable/function edit. |
| `set_physics_properties` | varies | Physics body settings. |

## Blueprint Graphs

| Tool | Key arguments | Notes |
|---|---|---|
| `add_node` | varies | Adds a node to the EventGraph (23+ node types). |
| `add_event_node` | varies | Event entry node. |
| `connect_nodes` | varies | Wires pins between nodes. |
| `delete_node` | varies | Removes a node. |
| `set_node_property` | varies | Sets a node's property value. |

## Blueprint Variables and Functions

| Tool | Key arguments | Notes |
|---|---|---|
| `create_variable` | varies | New BP variable. |
| `set_blueprint_variable_properties` | varies | Default value, category, etc. |
| `get_blueprint_variable_details` | varies | Inspect a variable. |
| `create_function` | varies | New function. |
| `add_function_input` / `add_function_output` | varies | Function signature pins. |
| `rename_function` / `delete_function` | varies | Manage functions. |

## Blueprint Analysis (read-only)

| Tool | Notes |
|---|---|
| `read_blueprint_content` | Full BP structure dump. |
| `analyze_blueprint_graph` | Event graph execution flow. |
| `get_blueprint_function_details` | Function-level detail. |

## Materials

| Tool | Key arguments | Notes |
|---|---|---|
| `get_available_materials` | — | Lists materials usable by the apply tools. |
| `apply_material_to_actor` | varies | Level actor material override. |
| `apply_material_to_blueprint` | varies | BP component material override. |
| `get_actor_material_info` | varies | Inspect current material slots. |
| `set_mesh_material_color` | varies | Quick color change on a mesh. |
