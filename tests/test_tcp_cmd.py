"""
test_tcp_cmd.py
Send individual TCP commands to the ResolutePulse Manager ingest socket (127.0.0.1:1515).

Usage:
    python tests/test_tcp_cmd.py                        # interactive menu
    python tests/test_tcp_cmd.py --verb diagnostics
    python tests/test_tcp_cmd.py --verb collector_stop
    python tests/test_tcp_cmd.py --verb config_push --params '{"section":"patch_management","config":{"auto_install":false}}'
    python tests/test_tcp_cmd.py --policy web_blocking --data '{"action":"block","url":"evil.com"}'
    python tests/test_tcp_cmd.py --list                 # list all verbs
"""

import socket
import json
import sys
import time
import argparse

HOST = '127.0.0.1'
PORT = 1515
AGENT_ID = 'test-license-agent'

# ── All supported verbs ──────────────────────────────────────────────────────

MODULE_VERBS = {
    'diagnostics':      {'params': {},                         'desc': 'Return live counters (read-only)'},
    'status_request':   {'params': {},                         'desc': 'Force immediate status flush + STATUS_REPORT over mTLS'},
    'collector_stop':   {'params': {},                         'desc': 'Pause event collection + batch sender'},
    'collector_start':  {'params': {},                         'desc': 'Resume event collection + batch sender'},
    'fim_stop':         {'params': {},                         'desc': 'Pause File Integrity Monitoring'},
    'fim_start':        {'params': {},                         'desc': 'Resume File Integrity Monitoring'},
    'worker_restart':   {'params': {},                         'desc': 'Stop + re-spawn rp-webblock / rp-softblock'},
    'config_push':      {'params': {'section': '...', 'config': {}}, 'desc': 'Merge config section into config.json'},
    'agent_restart':    {'params': {},                         'desc': '⚠ Graceful agent restart (use carefully)'},
}

POLICY_TYPES = {
    'web_blocking':       'Block/unblock URLs via hosts file',
    'software_blocking':  'Block/unblock executables via registry',
    'patch':              'Trigger patch scan or install specific KBs',
    'antivirus':          'Trigger ClamAV quick_scan or full_scan',
    'status_request':     'Request a status report (legacy path)',
}


def send_raw(payload: dict) -> bool:
    body = json.dumps(payload)
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(5)
            s.connect((HOST, PORT))
            s.sendall(body.encode('utf-8'))
        return True
    except Exception as e:
        print(f'  ERROR: {e}')
        return False


def send_module(verb: str, params: dict = None, command_id: str = '') -> bool:
    if params is None:
        params = {}
    if not command_id:
        command_id = f'{AGENT_ID[:8]}-{verb}-{int(time.time()*1000)}'
    payload = {
        'command_type': 'module',
        'agent_id':     AGENT_ID,
        'command_id':   command_id,
        'verb':         verb,
        'params':       params,
    }
    print(f'  Verb       : {verb}')
    print(f'  CommandId  : {command_id}')
    if params:
        print(f'  Params     : {json.dumps(params)}')
    ok = send_raw(payload)
    print(f'  {"=> Sent OK" if ok else "=> FAILED"}')
    return ok


def send_policy(policy_type: str, policy_data: dict) -> bool:
    payload = {
        'agent_id':    AGENT_ID,
        'policy_type': policy_type,
        'policy_data': policy_data,
    }
    print(f'  Type : {policy_type}')
    print(f'  Data : {json.dumps(policy_data)}')
    ok = send_raw(payload)
    print(f'  {"=> Sent OK" if ok else "=> FAILED"}')
    return ok


def interactive_menu():
    while True:
        print('\n' + '=' * 52)
        print('  ResolutePulse TCP Command Sender')
        print(f'  Target  : {HOST}:{PORT}   Agent: {AGENT_ID}')
        print('=' * 52)
        print('  MODULE COMMANDS (command_type=module)')
        for i, (verb, info) in enumerate(MODULE_VERBS.items(), start=1):
            print(f'    {i:2d}. {verb:<22} - {info["desc"]}')
        print()
        print('  POLICY COMMANDS (legacy path)')
        for i, (pt, desc) in enumerate(POLICY_TYPES.items(), start=len(MODULE_VERBS)+1):
            print(f'    {i:2d}. {pt:<22} - {desc}')
        print()
        print('     q. Quit')
        print()

        choice = input('  Select: ').strip().lower()
        if choice in ('q', 'quit', 'exit'):
            break

        try:
            idx = int(choice)
        except ValueError:
            print('  Invalid selection.')
            continue

        verbs = list(MODULE_VERBS.keys())
        policies = list(POLICY_TYPES.keys())
        all_items = verbs + policies

        if idx < 1 or idx > len(all_items):
            print('  Out of range.')
            continue

        item = all_items[idx - 1]

        if item in verbs:
            print()
            params = {}
            if item == 'config_push':
                section = input('  Section name (e.g. patch_management): ').strip()
                raw = input('  Config JSON (e.g. {"auto_install":false}): ').strip()
                try:
                    cfg = json.loads(raw)
                except Exception:
                    print('  Invalid JSON, sending empty config.')
                    cfg = {}
                params = {'section': section, 'config': cfg}
            elif item == 'agent_restart':
                c = input('  This will restart the agent. Type YES to confirm: ').strip()
                if c != 'YES':
                    print('  Aborted.')
                    continue
            send_module(item, params)

        else:
            print()
            print(f'  Policy type: {item}')
            raw = input('  Policy data JSON: ').strip()
            try:
                data = json.loads(raw)
            except Exception:
                print('  Invalid JSON, aborting.')
                continue
            send_policy(item, data)


# ── Quick-send presets (non-interactive) ─────────────────────────────────────

PRESET_EXAMPLES = {
    'diagnostics':       ('module', 'diagnostics',       {}),
    'status_request':    ('module', 'status_request',    {}),
    'collector_stop':    ('module', 'collector_stop',     {}),
    'collector_start':   ('module', 'collector_start',    {}),
    'fim_stop':          ('module', 'fim_stop',           {}),
    'fim_start':         ('module', 'fim_start',          {}),
    'worker_restart':    ('module', 'worker_restart',     {}),
    'agent_restart':     ('module', 'agent_restart',      {}),
}


def main():
    parser = argparse.ArgumentParser(description='ResolutePulse TCP command sender')
    parser.add_argument('--host',     default=HOST,     help='Manager host (default: 127.0.0.1)')
    parser.add_argument('--port',     default=PORT, type=int, help='Ingest port (default: 1515)')
    parser.add_argument('--agent',    default=AGENT_ID, help='Agent ID')
    parser.add_argument('--verb',     help='MODULE_COMMAND verb to send')
    parser.add_argument('--params',   default='{}',     help='JSON params for verb (for config_push etc.)')
    parser.add_argument('--policy',   help='Legacy policy_type to send')
    parser.add_argument('--data',     default='{}',     help='JSON policy_data for --policy')
    parser.add_argument('--list',     action='store_true', help='List all verbs and exit')
    args = parser.parse_args()

    global HOST, PORT, AGENT_ID
    HOST     = args.host
    PORT     = args.port
    AGENT_ID = args.agent

    if args.list:
        print('\nMODULE_COMMAND verbs:')
        for v, info in MODULE_VERBS.items():
            print(f'  {v:<24} {info["desc"]}')
        print('\nLegacy policy types:')
        for pt, desc in POLICY_TYPES.items():
            print(f'  {pt:<24} {desc}')
        sys.exit(0)

    if args.verb:
        try:
            params = json.loads(args.params)
        except Exception:
            print(f'Error: --params is not valid JSON: {args.params}')
            sys.exit(1)
        print(f'\n[MODULE_COMMAND: {args.verb}]')
        ok = send_module(args.verb, params)
        sys.exit(0 if ok else 1)

    if args.policy:
        try:
            data = json.loads(args.data)
        except Exception:
            print(f'Error: --data is not valid JSON: {args.data}')
            sys.exit(1)
        print(f'\n[POLICY: {args.policy}]')
        ok = send_policy(args.policy, data)
        sys.exit(0 if ok else 1)

    # No flags - run interactive menu
    interactive_menu()


if __name__ == '__main__':
    main()
