import ipaddress as _ipaddress
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import CONF_ID, CONF_NAME, CONF_TRIGGER_ID

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

# ── config keys ───────────────────────────────────────────────────────────────
CONF_NODES               = "nodes"
CONF_PORT                = "port"
CONF_MULTICAST_GROUP     = "multicast_group"
CONF_PROPOSAL_TIMEOUT_MS = "proposal_timeout_ms"
CONF_COMMANDS            = "commands"
CONF_REQUIRED_NODES      = "required_nodes"
CONF_ON_EXECUTE          = "on_execute"
CONF_KEY                 = "key"

# ── sub-schemas ───────────────────────────────────────────────────────────────
COMMAND_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_NAME):           cv.string_strict,
        cv.Optional(CONF_REQUIRED_NODES): cv.ensure_list(cv.string),
        cv.Optional(CONF_ON_EXECUTE):     automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(BarrierGroupOnExecuteTrigger)}
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
            
            # Merge commands (concatenate first, merged by name in step 2)
            if CONF_COMMANDS in group_conf:
                if CONF_COMMANDS in existing:
                    existing[CONF_COMMANDS].extend(group_conf[CONF_COMMANDS])
                else:
                    existing[CONF_COMMANDS] = group_conf[CONF_COMMANDS]
            
            # Merge other options, taking the last defined
            for opt in [CONF_PORT, CONF_MULTICAST_GROUP, CONF_PROPOSAL_TIMEOUT_MS, CONF_KEY]:
                if opt in group_conf:
                    existing[opt] = group_conf[opt]

    # 2. For each merged group, perform validation and merge internal commands by name
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
        if CONF_COMMANDS not in group_conf:
            raise cv.Invalid(f"Group '{group_id}' is missing the 'commands' list")

        # Merge commands inside this group by name
        commands = group_conf[CONF_COMMANDS]
        merged_cmds = {}
        for cmd in commands:
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
        
        group_conf[CONF_COMMANDS] = list(merged_cmds.values())

        # Validate nodes and commands
        all_nodes = set(group_conf[CONF_NODES])
        for cmd in group_conf[CONF_COMMANDS]:
            if CONF_REQUIRED_NODES not in cmd:
                raise cv.Invalid(
                    f"Command '{cmd[CONF_NAME]}' in group '{group_id}' is missing 'required_nodes'"
                )
            for req in cmd[CONF_REQUIRED_NODES]:
                if req not in all_nodes:
                    raise cv.Invalid(
                        f"Command '{cmd[CONF_NAME]}' in group '{group_id}': "
                        f"required_node '{req}' is not listed in nodes"
                    )
        
        final_groups.append(group_conf)

    return final_groups


# ── root schema ───────────────────────────────────────────────────────────────
SINGLE_GROUP_SCHEMA = cv.Schema(
    {
        cv.GenerateID():                       cv.declare_id(BarrierGroupComponent),
        cv.Optional(CONF_NODES):               cv.ensure_list(cv.string),
        cv.Optional(CONF_COMMANDS):            cv.ensure_list(COMMAND_SCHEMA),
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
    for cmd in config[CONF_COMMANDS]:
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

        for command in group_conf[CONF_COMMANDS]:
            # Convert hostname list → sorted integer IDs
            req_ids = [nodes.index(n) for n in command[CONF_REQUIRED_NODES]]
            req_nodes_expr = cg.RawExpression(
                "std::vector<uint8_t>{" + ", ".join(str(i) for i in req_ids) + "}"
            )

            trigger = None
            if CONF_ON_EXECUTE in command:
                for action_conf in command[CONF_ON_EXECUTE]:
                    trigger = cg.new_Pvariable(action_conf[CONF_TRIGGER_ID], var)
                    await automation.build_automation(trigger, [], action_conf)

            trigger_expr = trigger if trigger is not None else cg.RawExpression("nullptr")
            cg.add(var.add_command(command[CONF_NAME], req_nodes_expr, trigger_expr))


# ── barrier_group.propose action ──────────────────────────────────────────────
@automation.register_action(
    "barrier_group.propose",
    BarrierGroupProposeAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(BarrierGroupComponent),
            cv.Required("command"): cv.string_strict,
        }
    ),
)
async def barrier_group_propose_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent, config["command"])
    return var
