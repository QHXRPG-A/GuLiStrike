"""Small checked graph authoring helper. Runs inside Unreal Editor Python only."""
import unreal

B = unreal.BlueprintService


def literal(value):
    if isinstance(value, bool):
        return 'true' if value else 'false'
    return str(value)


def require(ok, context):
    if not ok:
        raise RuntimeError(context)
    return ok


class Pin:
    def __init__(self, graph, node, name):
        self.g, self.node, self.name = graph, node, name

    def __add__(self, value):
        return self.g.math('Add', self, value)

    def __sub__(self, value):
        return self.g.math('Subtract', self, value)

    def __mul__(self, value):
        return self.g.math('Multiply', self, value)

    def __truediv__(self, value):
        return self.g.math('Divide', self, value)


class Node:
    def __init__(self, graph, guid):
        require(guid, f'Node creation failed in {graph.bp}:{graph.name}')
        self.g, self.guid = graph, guid
        self.pins = {p.pin_name: p for p in B.get_node_pins(graph.bp, graph.name, guid)}

    def __getitem__(self, name):
        require(name in self.pins, f'Missing pin {name}; available {list(self.pins)} in {self.g.name}')
        return Pin(self.g, self, name)

    def put(self, name, value):
        dest = self[name]
        if isinstance(value, Pin):
            require(B.connect_nodes(self.g.bp, self.g.name, value.node.guid, value.name, self.guid, name),
                    f'Cannot connect {value.name} -> {name} in {self.g.name}')
        else:
            require(B.set_node_pin_value(self.g.bp, self.g.name, self.guid, name, literal(value)),
                    f'Cannot set {name} = {value} in {self.g.name}')
        # Wildcard nodes can change their other pin types after connection.
        self.pins = {p.pin_name: p for p in B.get_node_pins(self.g.bp, self.g.name, self.guid)}
        return dest


class Graph:
    def __init__(self, bp, name, function=False, clear=True):
        self.bp, self.name, self.n, self.tail = bp, name, 0, None
        self.getters = {}
        nodes = B.get_nodes_in_graph(bp, name)
        if clear:
            for n in nodes:
                if 'FunctionEntry' not in n.node_type and 'FunctionResult' not in n.node_type:
                    require(B.delete_node(bp, name, n.node_id), 'Clear owned graph node')
            nodes = B.get_nodes_in_graph(bp, name)
        if function:
            entry = next(n for n in nodes if 'FunctionEntry' in n.node_type)
            self.entry = Node(self, entry.node_id)
            self.tail = self.entry['then']
        else:
            self.entry = None

    def pos(self):
        n = self.n
        self.n += 1
        return ((n % 8) * 280, (n // 8) * 280)

    def call(g, owner, name, **args):
        n = Node(g, B.add_function_call_node(g.bp, g.name, owner, name, *g.pos()))
        for k, v in args.items():
            n.put(k, v)
        if 'execute' in n.pins and g.tail:
            n.put('execute', g.tail)
            g.tail = n['then']
        return n

    def native(self, name, **args):
        return self.call('KismetMathLibrary', name, **args)['ReturnValue']

    def method(self, owner, name, target, **args):
        return self.call(owner, name, self=target, **args)

    def cast(self, target_class, value):
        if target_class.startswith('/Game/'):
            target_class = unreal.load_asset(target_class).generated_class().get_name()
        n = Node(self, B.add_cast_node(self.bp, self.name, target_class, *self.pos()))
        n.put('Object', value)
        n.put('execute', self.tail)
        self.tail = n['then']
        return n[next(p for p in n.pins if p.startswith('As'))]

    def invoke(self, name, **args):
        return self.call(self.bp, name, **args)

    def get(self, name):
        if name not in self.getters:
            if name == 'self':
                self.getters[name] = Node(self, B.create_node_by_key(self.bp, self.name, 'NODE K2Node_Self', *self.pos()))['self']
            else:
                self.getters[name] = Node(self, B.add_get_variable_node(self.bp, self.name, name, *self.pos()))[name]
        return self.getters[name]

    def set(self, name, value):
        n = Node(self, B.add_set_variable_node(self.bp, self.name, name, *self.pos()))
        n.put(name, value)
        if self.tail:
            n.put('execute', self.tail)
        self.tail = n['then']
        return n

    def arg(self, name):
        return self.entry[name]

    def event(self, name):
        n = Node(self, B.add_event_node(self.bp, self.name, name, *self.pos()))
        self.tail = n['then']
        return n

    def branch(self, condition):
        n = Node(self, B.add_branch_node(self.bp, self.name, *self.pos()))
        n.put('Condition', condition)
        n.put('execute', self.tail)
        self.tail = n['then']
        return n

    def math(self, op, a, b):
        n = Node(self, B.add_math_node(self.bp, self.name, op, 'Float', *self.pos()))
        n.put('A', a)
        n.put('B', b)
        return n['ReturnValue']

    def compare(self, op, a, b, kind='Float'):
        n = Node(self, B.add_comparison_node(self.bp, self.name, op, kind, *self.pos()))
        n.put('A', a)
        n.put('B', b)
        return n['ReturnValue']

    def both(self, *conditions):
        result = conditions[0]
        for condition in conditions[1:]:
            result = self.native('BooleanAND', A=result, B=condition)
        return result

    def either(self, a, b):
        return self.native('BooleanOR', A=a, B=b)

    def select(self, yes, no, condition):
        return self.native('SelectFloat', A=yes, B=no, bPickA=condition)

    def clamp(self, value, minimum=0, maximum=1):
        return self.native('FClamp', Value=value, Min=minimum, Max=maximum)

    def ease(self, value, mode='EaseOut'):
        return self.native('Ease', A=0, B=1, Alpha=self.clamp(value), EasingFunc=mode, BlendExp=3)

    def vec(self, x=0, y=0, z=0):
        return self.native('MakeVector', X=x, Y=y, Z=z)

    def rot(self, roll=0, pitch=0, yaw=0):
        return self.native('MakeRotator', Roll=roll, Pitch=pitch, Yaw=yaw)

    def layout(self):
        B.auto_layout_graph(self.bp, self.name)


def variable(bp, name, kind, default='', editable=False, category='Card Reveal'):
    if not B.variable_exists(bp, name):
        require(B.add_variable(bp, name, kind, literal(default)), f'Add variable {name}')
    if default != '':
        require(B.set_variable_default_value(bp, name, literal(default)), f'Default {name}')
    require(B.modify_variable(bp, name, new_category=category, set_instance_editable=int(editable)), name)


def function(bp, name, inputs=()):
    if B.function_exists(bp, name):
        B.delete_function(bp, name)
    require(B.create_function(bp, name, False), f'Function {name}')
    for n, t in inputs:
        require(B.add_function_input(bp, name, n, t), f'Parameter {name}.{n}')


def compile_save(bp):
    result = B.compile_blueprint(bp)
    require(result.success, f'{bp}: {list(result.errors)}')
    require(unreal.EditorAssetLibrary.save_asset(bp, False), f'Save {bp}')
    return {'success': result.success, 'errors': list(result.errors), 'warnings': list(result.warnings)}
