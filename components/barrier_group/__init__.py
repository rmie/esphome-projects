import ipaddress as _ipaddress
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import CONF_ID, CONF_NAME, CONF_TRIGGER_ID
from esphome.core import CORE

barrier_group_ns = cg.esphome_ns.namespace("barrier_group")

BarrierGroupComponent = barrier_group_ns.class_(
    "BarrierGroupComponent", cg.Component
)
BarrierGroupProposeAction = barrier_group_ns.class_(
    "BarrierGroupProposeAction", automation.Action
)
BarrierGroupOnExecuteTrigger = barrier_group_ns.class_(
    "BarrierGroupOnExecuteTrigger", automation.Trigger.template()
)
BarrierGroupOnTimeoutTrigger = barrier_group_ns.class_(
    "BarrierGroupOnTimeoutTrigger", automation.Trigger.template()
)

# ── config keys ───────────────────────────────────────────────────────────────
CONF_NODES               = "nodes"
# ── sub-schemas ───────────────────────────────────────────────────────────────
CONF_PORT                = "port"
CONF_MULTICAST_GROUP     = "multicast_group"
CONF_PROPOSAL_TIMEOUT_MS = "proposal_timeout_ms"
CONF_PROPOSALS           = "proposals"
CONF_REQUIRED_NODES      = "required_nodes"
CONF_ON_EXECUTE          = "on_execute"
CONF_ON_TIMEOUT          = "on_timeout"
CONF_ACCEPT_IF           = "accept_if"
CONF_KEY                 = "key"
CONF_STATE_VARS          = "state_vars"

ALLOWED_TYPES = {
    "float": "float",
    "double": "double",
    "bool": "bool",
    "int": "int32_t",
    "uint": "uint32_t",
    "int8": "int8_t",
    "uint8": "uint8_t",
    "int16": "int16_t",
    "uint16": "uint16_t",
    "int32": "int32_t",
    "uint32": "uint32_t",
    "int8_t": "int8_t",
    "uint8_t": "uint8_t",
    "int16_t": "int16_t",
    "uint16_t": "uint16_t",
    "int32_t": "int32_t",
    "uint32_t": "uint32_t",
}

PROPOSAL_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_NAME):           cv.string_strict,
        cv.Optional(CONF_REQUIRED_NODES): cv.ensure_list(cv.string),
        cv.Optional(CONF_ON_EXECUTE):     automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(automation.Trigger)}
        ),
        cv.Optional(CONF_ON_TIMEOUT):     automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(BarrierGroupOnTimeoutTrigger)}
        ),
        cv.Optional(CONF_ACCEPT_IF):      cv.returning_lambda,
        cv.Optional(CONF_STATE_VARS):     cv.Schema(
            {
                cv.string_strict: cv.one_of(*ALLOWED_TYPES, lower=True)
            }
        ),
    }
)


def _validate_group_list(config_list):
    # 1. Merge duplicate groups by ID
    merged_groups = {}
    for group_conf in config_list:
        group_id = str(group_conf[CONF_ID])
        if group_id not in merged_groups:
            merged_groups[group_id] = dict(group_conf)
        else:
            existing = merged_groups[group_id]
            # Merge nodes
            if CONF_NODES in group_conf:
                if CONF_NODES in existing:
                    # Union of nodes
                    merged_nodes = list(existing[CONF_NODES])
                    for n in group_conf[CONF_NODES]:
                        if n not in merged_nodes:
                            merged_nodes.append(n)
                    existing[CONF_NODES] = merged_nodes
                else:
                    existing[CONF_NODES] = group_conf[CONF_NODES]
            
            # Merge proposals (concatenate first, merged by name in step 2)
            if CONF_PROPOSALS in group_conf:
                if CONF_PROPOSALS in existing:
                    existing[CONF_PROPOSALS].extend(group_conf[CONF_PROPOSALS])
                else:
                    existing[CONF_PROPOSALS] = group_conf[CONF_PROPOSALS]
            
            # Merge other options, taking the last defined
            for opt in [CONF_PORT, CONF_MULTICAST_GROUP, CONF_PROPOSAL_TIMEOUT_MS, CONF_KEY]:
                if opt in group_conf:
                    existing[opt] = group_conf[opt]

    # 2. For each merged group, perform validation and merge internal proposals by name
    final_groups = []
    for group_id, group_conf in merged_groups.items():
        # Set defaults if not present
        if CONF_PORT not in group_conf:
            group_conf[CONF_PORT] = 6543
        if CONF_MULTICAST_GROUP not in group_conf:
            group_conf[CONF_MULTICAST_GROUP] = "239.1.2.3"
        if CONF_PROPOSAL_TIMEOUT_MS not in group_conf:
            group_conf[CONF_PROPOSAL_TIMEOUT_MS] = 2000

        # Enforce required fields
        if CONF_NODES not in group_conf:
            raise cv.Invalid(f"Group '{group_id}' is missing the 'nodes' list")
        if CONF_PROPOSALS not in group_conf:
            raise cv.Invalid(f"Group '{group_id}' is missing the 'proposals' list")

        # Merge proposals inside this group by name
        proposals = group_conf[CONF_PROPOSALS]
        merged_cmds = {}
        for cmd in proposals:
            name = cmd[CONF_NAME]
            if name not in merged_cmds:
                merged_cmds[name] = dict(cmd)
            else:
                existing_cmd = merged_cmds[name]
                if CONF_REQUIRED_NODES in cmd:
                    if CONF_REQUIRED_NODES in existing_cmd:
                        merged_reqs = list(existing_cmd[CONF_REQUIRED_NODES])
                        for req in cmd[CONF_REQUIRED_NODES]:
                            if req not in merged_reqs:
                                merged_reqs.append(req)
                        existing_cmd[CONF_REQUIRED_NODES] = merged_reqs
                    else:
                        existing_cmd[CONF_REQUIRED_NODES] = cmd[CONF_REQUIRED_NODES]
                if CONF_ON_EXECUTE in cmd:
                    if CONF_ON_EXECUTE in existing_cmd:
                        existing_cmd[CONF_ON_EXECUTE].extend(cmd[CONF_ON_EXECUTE])
                    else:
                        existing_cmd[CONF_ON_EXECUTE] = cmd[CONF_ON_EXECUTE]
                if CONF_ON_TIMEOUT in cmd:
                    if CONF_ON_TIMEOUT in existing_cmd:
                        existing_cmd[CONF_ON_TIMEOUT].extend(cmd[CONF_ON_TIMEOUT])
                    else:
                        existing_cmd[CONF_ON_TIMEOUT] = cmd[CONF_ON_TIMEOUT]
                if CONF_ACCEPT_IF in cmd:
                    existing_cmd[CONF_ACCEPT_IF] = cmd[CONF_ACCEPT_IF]
                if CONF_STATE_VARS in cmd:
                    existing_cmd[CONF_STATE_VARS] = cmd[CONF_STATE_VARS]
        
        group_conf[CONF_PROPOSALS] = list(merged_cmds.values())

        # Validate nodes and proposals
        all_nodes = set(group_conf[CONF_NODES])
        for cmd in group_conf[CONF_PROPOSALS]:
            if CONF_REQUIRED_NODES not in cmd:
                raise cv.Invalid(
                    f"Proposal '{cmd[CONF_NAME]}' in group '{group_id}' is missing 'required_nodes'"
                )
            for req in cmd[CONF_REQUIRED_NODES]:
                if req not in all_nodes:
                    raise cv.Invalid(
                        f"Proposal '{cmd[CONF_NAME]}' in group '{group_id}': "
                        f"required_node '{req}' is not listed in nodes"
                    )
        
        final_groups.append(group_conf)

    return final_groups


# ── root schema ───────────────────────────────────────────────────────────────
SINGLE_GROUP_SCHEMA = cv.Schema(
    {
        cv.GenerateID():                       cv.declare_id(BarrierGroupComponent),
        cv.Optional(CONF_NODES):               cv.ensure_list(cv.string),
        cv.Optional(CONF_PROPOSALS):           cv.ensure_list(PROPOSAL_SCHEMA),
        cv.Optional(CONF_PORT):                cv.port,
        cv.Optional(CONF_MULTICAST_GROUP):     cv.string,
        cv.Optional(CONF_PROPOSAL_TIMEOUT_MS): cv.positive_int,
        cv.Optional(CONF_KEY):                 cv.string_strict,
    }
).extend(cv.COMPONENT_SCHEMA)

CONFIG_SCHEMA = cv.All(
    cv.ensure_list(SINGLE_GROUP_SCHEMA),
    _validate_group_list,
)


def fnv1a_32(data: str) -> int:
    hash_val = 0x811c9dc5
    for char in data:
        hash_val ^= ord(char)
        hash_val = (hash_val * 0x01000193) & 0xffffffff
    return hash_val


def generate_group_hash(config) -> int:
    parts = [str(config[CONF_ID])]
    
    nodes = sorted(config[CONF_NODES])
    parts.append(",".join(nodes))
    
    parts.append(str(config[CONF_PORT]))
    parts.append(str(config[CONF_MULTICAST_GROUP]))
    parts.append(str(config[CONF_PROPOSAL_TIMEOUT_MS]))
    
    cmds = []
    for cmd in config[CONF_PROPOSALS]:
        cmd_name = cmd[CONF_NAME]
        reqs = sorted(cmd[CONF_REQUIRED_NODES])
        cmds.append(f"{cmd_name}:{','.join(reqs)}")
    parts.append(";".join(sorted(cmds)))
    
    config_str = "|".join(parts)
    return fnv1a_32(config_str)


async def to_code(config):
    for group_conf in config:
        var = cg.new_Pvariable(group_conf[CONF_ID])
        await cg.register_component(var, group_conf)

        group_id = generate_group_hash(group_conf)
        cg.add(var.set_group_id(group_id))

        if CONF_KEY in group_conf:
            cg.add(var.set_key(group_conf[CONF_KEY]))

        cg.add(var.set_port(group_conf[CONF_PORT]))
        cg.add(var.set_proposal_timeout_ms(group_conf[CONF_PROPOSAL_TIMEOUT_MS]))

        mc_int = int(_ipaddress.IPv4Address(group_conf[CONF_MULTICAST_GROUP]))
        cg.add(var.set_multicast_group(mc_int))

        # Sort nodes alphabetically — this gives every device the same deterministic
        # ID assignment without any per-device configuration.
        nodes = sorted(group_conf[CONF_NODES])
        for node_id, node_name in enumerate(nodes):
            cg.add(var.add_node(node_id, node_name))

        for command in group_conf[CONF_PROPOSALS]:
            # Convert hostname list → sorted integer IDs
            req_ids = [nodes.index(n) for n in command[CONF_REQUIRED_NODES]]
            req_nodes_expr = cg.RawExpression(
                "std::vector<uint8_t>{" + ", ".join(str(i) for i in req_ids) + "}"
            )

            state_vars = command.get(CONF_STATE_VARS, {})
            struct_name = f"{group_conf[CONF_ID]}_{command[CONF_NAME]}_State"

            # Generate and add packed C++ state struct globally
            if state_vars:
                struct_str = f"struct __attribute__((packed)) {struct_name} {{\n"
                for var_name, var_type in sorted(state_vars.items()):
                    cpp_type = ALLOWED_TYPES[var_type]
                    struct_str += f"    {cpp_type} {var_name};\n"
                struct_str += "};\n"
                cg.add_global(cg.RawStatement(struct_str))
                state_size_expr = cg.RawExpression(f"sizeof({struct_name})")
            else:
                state_size_expr = cg.RawExpression("0")

            execute_trigger = None
            if CONF_ON_EXECUTE in command:
                for action_conf in command[CONF_ON_EXECUTE]:
                    if state_vars:
                        trigger_type = barrier_group_ns.class_("BarrierGroupOnExecuteTrigger").template(cg.RawExpression(struct_name))
                        trigger_args = [(cg.RawExpression(f"const {struct_name} &"), "state")]
                    else:
                        trigger_type = barrier_group_ns.class_("BarrierGroupOnExecuteTrigger").template(cg.void)
                        trigger_args = []

                    action_conf[CONF_TRIGGER_ID].type = trigger_type
                    execute_trigger = cg.new_Pvariable(action_conf[CONF_TRIGGER_ID])
                    await automation.build_automation(execute_trigger, trigger_args, action_conf)

            if execute_trigger is not None:
                if state_vars:
                    callback_expr = cg.RawExpression(
                        f"[=](const void *state_ptr) {{ {execute_trigger}->trigger(*reinterpret_cast<const {struct_name} *>(state_ptr)); }}"
                    )
                else:
                    callback_expr = cg.RawExpression(
                        f"[=](const void *state_ptr) {{ (void)state_ptr; {execute_trigger}->trigger(); }}"
                    )
            else:
                callback_expr = cg.RawExpression("nullptr")

            timeout_trigger = None
            if CONF_ON_TIMEOUT in command:
                for action_conf in command[CONF_ON_TIMEOUT]:
                    timeout_trigger = cg.new_Pvariable(action_conf[CONF_TRIGGER_ID])
                    await automation.build_automation(timeout_trigger, [], action_conf)
            timeout_trigger_expr = timeout_trigger if timeout_trigger is not None else cg.RawExpression("nullptr")

            accept_if_expr = cg.RawExpression("nullptr")
            if CONF_ACCEPT_IF in command:
                if state_vars:
                    args = [(cg.RawExpression(f"const {struct_name} &"), "state")]
                    lambda_expr = await cg.process_lambda(command[CONF_ACCEPT_IF], args, return_type=cg.bool_)
                    accept_if_expr = cg.RawExpression(
                        f"[=](const void *state_ptr) -> bool {{ return {lambda_expr}(*reinterpret_cast<const {struct_name} *>(state_ptr)); }}"
                    )
                else:
                    lambda_expr = await cg.process_lambda(command[CONF_ACCEPT_IF], [], return_type=cg.bool_)
                    accept_if_expr = cg.RawExpression(
                        f"[=](const void *state_ptr) -> bool {{ (void)state_ptr; return {lambda_expr}(); }}"
                    )

            cg.add(var.add_proposal(command[CONF_NAME], req_nodes_expr, state_size_expr, callback_expr, timeout_trigger_expr, accept_if_expr))


# ── barrier_group.propose action ──────────────────────────────────────────────
@automation.register_action(
    "barrier_group.propose",
    automation.Action, # Used as placeholder, we dynamically compile custom class
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(BarrierGroupComponent),
            cv.Required("proposal"): cv.string_strict,
            cv.Optional("state"): cv.Schema(
                {
                    cv.string_strict: cv.templatable(cv.valid)
                }
            ),
        }
    ),
)
async def barrier_group_propose_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    
    # 1. Lookup parent group config to find proposal schema
    group_conf = None
    for gc in CORE.config.get("barrier_group", []):
        if gc[CONF_ID] == config[CONF_ID]:
            group_conf = gc
            break
            
    if group_conf is None:
        raise cv.Invalid("Associated barrier_group component not found")

    proposal_conf = None
    for pc in group_conf.get(CONF_PROPOSALS, []):
        if pc[CONF_NAME] == config["proposal"]:
            proposal_conf = pc
            break

    if proposal_conf is None:
        raise cv.Invalid(f"Proposal '{config['proposal']}' not found in group '{group_conf[CONF_ID]}'")

    state_vars = proposal_conf.get(CONF_STATE_VARS, {})
    struct_name = f"{group_conf[CONF_ID]}_{config['proposal']}_State"
    action_class_name = f"ProposeAction_{action_id}"

    # 2. Validate state schema vs configuration
    if state_vars:
        if "state" not in config:
            raise cv.Invalid(f"Proposal '{config['proposal']}' requires 'state' variables configuration")
        state_config = config["state"]
        for k in state_vars:
            if k not in state_config:
                raise cv.Invalid(f"Missing required state variable '{k}' for proposal '{config['proposal']}'")
        for k in state_config:
            if k not in state_vars:
                raise cv.Invalid(f"Unexpected state variable '{k}' for proposal '{config['proposal']}' in action")
    else:
        if "state" in config:
            raise cv.Invalid(f"Proposal '{config['proposal']}' does not accept 'state' variables")

    # 3. Generate Action C++ class definition dynamically
    class_str = f"class {action_class_name} : public esphome::Action<> {{\n"
    class_str += " public:\n"
    class_str += f"  {action_class_name}(esphome::barrier_group::BarrierGroupComponent *parent) : parent_(parent) {{}}\n"
    
    for var_name, var_type in sorted(state_vars.items()):
        cpp_type = ALLOWED_TYPES[var_type]
        class_str += f"  void set_{var_name}(std::function<{cpp_type}()> {var_name}) {{ {var_name}_ = {var_name}; }}\n"

    class_str += "  void play() override {\n"
    if state_vars:
        class_str += f"    {struct_name} state_data{{}};\n"
        for var_name, var_type in sorted(state_vars.items()):
            class_str += f"    if ({var_name}_) state_data.{var_name} = {var_name}_();\n"
        class_str += f"    parent_->propose(\"{config['proposal']}\", &state_data, sizeof(state_data));\n"
    else:
        class_str += f"    parent_->propose(\"{config['proposal']}\", nullptr, 0);\n"
    class_str += "  }\n"
    
    class_str += " private:\n"
    class_str += "  esphome::barrier_group::BarrierGroupComponent *parent_;\n"
    for var_name, var_type in sorted(state_vars.items()):
        cpp_type = ALLOWED_TYPES[var_type]
        class_str += f"  std::function<{cpp_type}()> {var_name}_;\n"
    class_str += "};\n"

    cg.add_global(cg.RawStatement(class_str))

    action_class = cg.global_ns.class_(action_class_name)
    action_id_copy = action_id.copy()
    action_id_copy.type = action_class
    var = cg.new_Pvariable(action_id_copy, parent)

    # 4. Compile lambdas/values and assign to setters
    if state_vars:
        state_config = config["state"]
        for var_name, var_type in sorted(state_vars.items()):
            cpp_type = ALLOWED_TYPES[var_type]
            val = state_config[var_name]
            if isinstance(val, automation.Lambda):
                template_expr = await cg.process_lambda(val, [], return_type=cg.RawExpression(cpp_type))
            else:
                if var_type == "bool":
                    cpp_val = "true" if val else "false"
                elif "float" in var_type or "double" in var_type:
                    cpp_val = f"{val}f"
                else:
                    cpp_val = str(val)
                template_expr = cg.RawExpression(f"[=]() -> {cpp_type} {{ return {cpp_val}; }}")
            cg.add(getattr(var, f"set_{var_name}")(template_expr))

    return var
