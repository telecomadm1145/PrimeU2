import sys
import json
import socket
import struct
import zlib
import base64
import time
import os

KEY_MAPPING = {
    'esc': 0x01,
    'left': 0x02,
    'up': 0x03,
    'right': 0x04,
    'down': 0x05,
    'back': 0x0C,
    'enter': 0x0D,
    'space': 0x20,
    'shift': 0x8B,
    '0': 0x30,
    '1': 0x59,
    '2': 0x5A,
    '3': 0x33,
    '4': 0x55,
    '5': 0x56,
    '6': 0x57,
    '7': 0x51,
    '8': 0x52,
    '9': 0x53,
    'x': 0x44,
    ',': 0x4F,
    '/': 0x54,
    '*': 0x58,
    '.': 0xB8,
    '+': 0xB9,
    '-': 0xB7,
    'var': 0x41,
    'toolbox': 0x42,
    'math': 0x43,
    'abc': 0x45,
    'pow': 0x46,
    'sin': 0x47,
    'cos': 0x48,
    'tan': 0x49,
    'ln': 0x4A,
    'log': 0x4B,
    'sqr': 0x4C,
    'sign': 0x4D,
    'paren': 0x4E,
    'exp': 0x50,
    'on': 0x83,
    'symb': 0x91,
    'msg': 0x93,
    'plot': 0xB2,
    'num': 0xB3,
    'view': 0xB4,
    'cas': 0xB5,
    'alpha': 0xB6,
    'help': 0x95,
    'apps': 0xB1,
    'home': 0xe047,
}

KEY_DESCRIPTIONS = {
    'esc': 'Escape / Cancel / Clear (with Shift)',
    'left': 'Arrow Left',
    'up': 'Arrow Up',
    'right': 'Arrow Right',
    'down': 'Arrow Down',
    'back': 'Backspace / Delete last character',
    'enter': 'Enter / Evaluate expression',
    'space': 'Space character / Underscore (with Shift)',
    'shift': 'Shift modifier key',
    '0': 'Number 0 / Notes (with Shift) / Alpha "',
    '1': 'Number 1 / Program (with Shift) / Alpha Y',
    '2': 'Number 2 / i (with Shift) / Alpha Z',
    '3': 'Number 3 / pi (with Shift) / Alpha #',
    '4': 'Number 4 / Matrix (with Shift) / Alpha U',
    '5': 'Number 5 / Alpha V',
    '6': 'Number 6 / Alpha W',
    '7': 'Number 7 / List (with Shift) / Alpha Q',
    '8': 'Number 8 / Alpha R',
    '9': 'Number 9 / Alpha S',
    'x': 'Variable X (xtθn) / Define (with Shift) / Alpha D',
    ',': 'Comma / Eval (with Shift) / Alpha O',
    '/': 'Division operator (/) / x^-1 (with Shift) / Alpha T',
    '*': 'Multiplication operator (*) / Alpha X',
    '.': 'Decimal point / = (with Shift)',
    '+': 'Addition operator (+) / Ans (with Shift) / Alpha ;',
    '-': 'Subtraction operator (-) / Base (with Shift) / Alpha :',
    'var': 'Variables menu (Vars) / Chars (with Shift) / Alpha A',
    'toolbox': 'Toolbox menu (Mem) / Alpha B',
    'math': 'Math template / Units (with Shift) / Alpha C',
    'abc': 'Fraction toggle (a b/c) / Alpha E',
    'pow': 'Power operator (x^y) / Alpha F',
    'sin': 'Sine function (SIN) / ASIN (with Shift) / Alpha G',
    'cos': 'Cosine function (COS) / ACOS (with Shift) / Alpha H',
    'tan': 'Tangent function (TAN) / ATAN (with Shift) / Alpha I',
    'ln': 'Natural Logarithm (LN) / e^x (with Shift) / Alpha J',
    'log': 'Logarithm base 10 (LOG) / 10^x (with Shift) / Alpha K',
    'sqr': 'Square operator (x^2) / Square root (with Shift) / Alpha L',
    'sign': 'Toggle positive/negative (+/-) / abs (with Shift) / Alpha M',
    'paren': 'Parentheses () / curly braces {} (with Shift) / Alpha N',
    'exp': 'Scientific exponent (EEX) / Sto (with Shift) / Alpha P',
    'on': 'Turn on / Off (with Shift) / Home button (on calculator)',
    'symb': 'Symbolic view (Symb) / Setup (with Shift)',
    'msg': 'Message center / Paste (with Shift)',
    'plot': 'Plot view (Plot) / Setup (with Shift)',
    'num': 'Numeric view (Num) / Setup (with Shift)',
    'view': 'View menu / Copy (with Shift)',
    'cas': 'Computer Algebra System mode / Settings (with Shift)',
    'alpha': 'Alpha mode key',
    'help': 'Help screen / User (with Shift)',
    'apps': 'Apps menu (Application Library) / Info (with Shift)',
    'home': 'Home screen (Main calculation area) / Settings (with Shift)',
}

def send_cmd(cmd_str):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(5.0)
        s.connect(('127.0.0.1', 4321))
        s.sendall((cmd_str + '\n').encode('utf-8'))
        
        if cmd_str.startswith('screenshot'):
            size_data = s.recv(4)
            if len(size_data) < 4:
                return "ERROR: Did not receive size prefix"
            size = struct.unpack('!I', size_data)[0]
            if size == 0:
                return "ERROR: LCD not active or size is 0"
            
            buf = bytearray()
            while len(buf) < size:
                chunk = s.recv(size - len(buf))
                if not chunk:
                    break
                buf.extend(chunk)
            s.close()
            return buf
        else:
            res = s.recv(1024).decode('utf-8')
            s.close()
            return res.strip()
    except Exception as e:
        return f"ERROR: Connection failed: {e}"

def bgra_to_png(bmp_data):
    width = 320
    height = 240
    pixels = bmp_data[54:]
    
    rgba = bytearray(width * height * 4)
    for i in range(width * height):
        idx = i * 4
        if idx + 2 < len(pixels):
            rgba[idx] = pixels[idx+2]     # R
            rgba[idx+1] = pixels[idx+1]   # G
            rgba[idx+2] = pixels[idx]     # B
            rgba[idx+3] = 255             # A
            
    # Standard PNG signature
    png = bytearray(b'\x89PNG\r\n\x1a\n')
    
    # IHDR chunk
    ihdr_data = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
    png += struct.pack('>I', 13) + b'IHDR' + ihdr_data + struct.pack('>I', zlib.crc32(b'IHDR' + ihdr_data))
    
    # IDAT chunk
    scanlines = []
    for y in range(height):
        scanlines.append(b'\x00' + rgba[y * width * 4 : (y + 1) * width * 4])
    idat_data = zlib.compress(b''.join(scanlines))
    png += struct.pack('>I', len(idat_data)) + b'IDAT' + idat_data + struct.pack('>I', zlib.crc32(b'IDAT' + idat_data))
    
    # IEND chunk
    png += struct.pack('>I', 0) + b'IEND' + struct.pack('>I', zlib.crc32(b'IEND'))
    return bytes(png)

def handle_request(req):
    method = req.get('method')
    params = req.get('params', {})
    req_id = req.get('id')
    
    if method == 'initialize':
        return {
            'jsonrpc': '2.0',
            'id': req_id,
            'result': {
                'protocolVersion': '2024-11-05',
                'capabilities': {
                    'tools': {}
                },
                'serverInfo': {
                    'name': 'primeu-mcp',
                    'version': '1.0.0'
                }
            }
        }
        
    elif method == 'ping':
        return {
            'jsonrpc': '2.0',
            'id': req_id,
            'result': {}
        }
        
    elif method == 'tools/list':
        return {
            'jsonrpc': '2.0',
            'id': req_id,
            'result': {
                'tools': [

                    {
                        'name': 'touch_screen',
                        'description': 'Simulate a batch of touch events on the calculator screen.',
                        'inputSchema': {
                            'type': 'object',
                            'properties': {
                                'touches': {
                                    'type': 'array',
                                    'items': {
                                        'type': 'object',
                                        'properties': {
                                            'x': {'type': 'integer', 'minimum': 0, 'maximum': 319},
                                            'y': {'type': 'integer', 'minimum': 0, 'maximum': 239},
                                            'action': {'type': 'string', 'enum': ['tap', 'down', 'move', 'up'], 'default': 'tap'},
                                            'delay_ms': {'type': 'integer', 'default': 50}
                                        },
                                        'required': ['x', 'y']
                                    }
                                }
                            },
                            'required': ['touches']
                        }
                    },
                    {
                        'name': 'get_screen',
                        'description': 'Capture the current screen of the calculator simulator. Returns base64 PNG data.',
                        'inputSchema': {
                            'type': 'object',
                            'properties': {}
                        }
                    },
                    {
                        'name': 'get_state',
                        'description': 'Retrieve the current state of the emulator.',
                        'inputSchema': {
                            'type': 'object',
                            'properties': {}
                        }
                    },
                    {
                        'name': 'get_buttons',
                        'description': 'Get a list of all valid calculator physical buttons and their brief descriptions.',
                        'inputSchema': {
                            'type': 'object',
                            'properties': {}
                        }
                    },
                    {
                        'name': 'press_button',
                        'description': 'Press a sequence of physical calculator buttons in order with a delay between them.',
                        'inputSchema': {
                            'type': 'object',
                            'properties': {
                                'keys': {
                                    'type': 'array',
                                    'items': {
                                        'type': 'string'
                                    },
                                    'description': 'Sequence of keys to press, e.g., ["1", "+", "2", "enter"]'
                                },
                                'delay_ms': {
                                    'type': 'integer',
                                    'default': 100,
                                    'description': 'Delay between button presses in milliseconds (default 100)'
                                }
                            },
                            'required': ['keys']
                        }
                    },
                    {
                        'name': 'input_string',
                        'description': 'Input a string of characters (letters, digits, spaces, and punctuation/symbols) into the active text field/editor.',
                        'inputSchema': {
                            'type': 'object',
                            'properties': {
                                'text': {
                                    'type': 'string',
                                    'description': 'String to input (e.g., "A < B;"). Supports A-Z, a-z, 0-9, spaces, and symbols like <, >, =, !, ", \', &, +, -, *, /, ,, ., ;, :, (, ), {, }, [, ], ^. All brackets and quotes must be balanced. Note: In CAS/Home views, the "/" character opens a Math Template instead of typing literally. In program editors, it types literally.'
                                },
                                'mode': {
                                    'type': 'string',
                                    'enum': ['PPL', 'CAS', 'Python'],
                                    'default': 'PPL',
                                    'description': 'Calculator case typing mode. PPL defaults to uppercase (lowercase requires shift). CAS and Python default to lowercase (uppercase requires shift).'
                                },
                                'delay_ms': {
                                    'type': 'integer',
                                    'default': 100,
                                    'description': 'Delay between button presses in milliseconds (default 100)'
                                }
                            },
                            'required': ['text']
                        }
                    },
                    {
                        'name': 'write_hpprgm',
                        'description': 'Directly write an HP PPL program into the emulator as a binary .hpprgm file using the legacy firmware format, bypassing slow typing.',
                        'inputSchema': {
                            'type': 'object',
                            'properties': {
                                'filename': {
                                    'type': 'string',
                                    'description': 'Program name without extension (e.g., "SNAKE").'
                                },
                                'content': {
                                    'type': 'string',
                                    'description': 'Full source code of the PPL program (e.g., "EXPORT SNAKE() BEGIN ... END;").'
                                }
                            },
                            'required': ['filename', 'content']
                        }
                    },
                    {
                        'name': 'create_hpappdir',
                        'description': 'Create a blank HP App directory template (.hpappdir) by cloning a base app (like the Function app) and renaming the internal .hpapp file. This prepares a blank app canvas for injecting custom programs.',
                        'inputSchema': {
                            'type': 'object',
                            'properties': {
                                'app_name': {
                                    'type': 'string',
                                    'description': 'Name of the new app without extension (e.g., "MyApp").'
                                }
                            },
                            'required': ['app_name']
                        }
                    }
                ]
            }
        }
        
    elif method == 'tools/call':
        tool_name = params.get('name')
        args = params.get('arguments', {})
        
        if tool_name == 'touch_screen':
            touches = args.get('touches', [])
            res_list = []
            is_err = False
            for t in touches:
                x = t.get('x')
                y = t.get('y')
                action = t.get('action', 'tap')
                delay_ms = t.get('delay_ms', 50)
                
                if action == 'tap':
                    r1 = send_cmd(f"touch {x} {y} down")
                    time.sleep(0.05)
                    r2 = send_cmd(f"touch {x} {y} up")
                    res_list.append(f"Tap: {r1} / {r2}")
                    if "ERROR" in r1 or "ERROR" in r2: is_err = True
                else:
                    r = send_cmd(f"touch {x} {y} {action}")
                    res_list.append(r)
                    if "ERROR" in r: is_err = True
                time.sleep(delay_ms / 1000.0)
                
            return {
                'jsonrpc': '2.0',
                'id': req_id,
                'result': {
                    'content': [{'type': 'text', 'text': " | ".join(res_list)}],
                    'isError': is_err
                }
            }
            
        elif tool_name == 'get_screen':
            res = send_cmd("screenshot")
            if isinstance(res, str) and res.startswith("ERROR"):
                return {
                    'jsonrpc': '2.0',
                    'id': req_id,
                    'result': {
                        'content': [{'type': 'text', 'text': res}],
                        'isError': True
                    }
                }
            
            try:
                png_bytes = bgra_to_png(res)
                
                # Also save the screenshot locally
                script_dir = os.path.dirname(os.path.abspath(__file__))
                screenshot_path = os.path.join(script_dir, "screenshot.png")
                with open(screenshot_path, "wb") as f:
                    f.write(png_bytes)
                
                b64_data = base64.b64encode(png_bytes).decode('utf-8')
                
                return {
                    'jsonrpc': '2.0',
                    'id': req_id,
                    'result': {
                        'content': [
                            {
                                'type': 'image',
                                'data': b64_data,
                                'mimeType': 'image/png'
                            },
                            {
                                'type': 'text',
                                'text': f"Screenshot saved locally to {screenshot_path}"
                            }
                        ]
                    }
                }
            except Exception as e:
                return {
                    'jsonrpc': '2.0',
                    'id': req_id,
                    'result': {
                        'content': [{'type': 'text', 'text': f"ERROR processing image: {e}"}],
                        'isError': True
                    }
                }
                
        elif tool_name == 'get_state':
            res = send_cmd("state")
            is_err = "ERROR" in res
            return {
                'jsonrpc': '2.0',
                'id': req_id,
                'result': {
                    'content': [{'type': 'text', 'text': res}],
                    'isError': is_err
                }
            }
            
        elif tool_name == 'get_buttons':
            lines = [f"{k.upper():<10} : {v}" for k, v in KEY_DESCRIPTIONS.items()]
            text = "Valid keys for 'press_button':\n\n" + "\n".join(lines)
            return {
                'jsonrpc': '2.0',
                'id': req_id,
                'result': {
                    'content': [{'type': 'text', 'text': text}],
                    'isError': False
                }
            }
            
        elif tool_name == 'press_button':
            keys = args.get('keys', [])
            delay_ms = args.get('delay_ms', 100)
            
            # Validation
            for key in keys:
                key_lower = key.lower()
                if key_lower not in KEY_MAPPING:
                    return {
                        'jsonrpc': '2.0',
                        'id': req_id,
                        'result': {
                            'content': [{'type': 'text', 'text': f"ERROR: Key '{key}' not supported."}],
                            'isError': True
                        }
                    }
            
            for key in keys:
                key_lower = key.lower()
                keycode = KEY_MAPPING[key_lower]
                send_cmd(f"key {keycode} press")
                time.sleep(delay_ms / 1000.0)
                
            return {
                'jsonrpc': '2.0',
                'id': req_id,
                'result': {
                    'content': [{'type': 'text', 'text': f"OK (Pressed {len(keys)} keys)"}],
                    'isError': False
                }
            }
            
        elif tool_name == 'input_string':
            text = args.get('text', '')
            delay_ms = args.get('delay_ms', 100)
            mode = args.get('mode', 'PPL').upper()
            if mode not in ('PPL', 'CAS', 'PYTHON'):
                mode = 'PPL'
                
            # Balanced bracket check
            def check_balanced(s):
                if s.count('"') % 2 != 0:
                    return False, "Unbalanced double quotes (\") detected."
                paren = 0
                brace = 0
                bracket = 0
                for char in s:
                    if char == '(':
                        paren += 1
                    elif char == ')':
                        paren -= 1
                        if paren < 0:
                            return False, "Unmatched closing parenthesis ')' detected."
                    elif char == '{':
                        brace += 1
                    elif char == '}':
                        brace -= 1
                        if brace < 0:
                            return False, "Unmatched closing brace '}' detected."
                    elif char == '[':
                        bracket += 1
                    elif char == ']':
                        bracket -= 1
                        if bracket < 0:
                            return False, "Unmatched closing bracket ']' detected."
                if paren != 0:
                    return False, f"Unbalanced parentheses: {paren} unclosed open parenthes(es)."
                if brace != 0:
                    return False, f"Unbalanced braces: {brace} unclosed open brace(s)."
                if bracket != 0:
                    return False, f"Unbalanced brackets: {bracket} unclosed open bracket(s)."
                return True, ""

            balanced, err_msg = check_balanced(text)
            if not balanced:
                return {
                    'jsonrpc': '2.0',
                    'id': req_id,
                    'result': {
                        'content': [{
                            'type': 'text', 
                            'text': f"ERROR: {err_msg}\n\nAll parentheses, brackets, braces, and quotes MUST be perfectly balanced because the calculator editor uses auto-closing features. If you must input unbalanced code, please perform the keystrokes manually using 'press_button' or 'touch_screen'."
                        }],
                        'isError': True
                    }
                }
            
            CHAR_TO_KEY = {
                'A': 'var', 'B': 'toolbox', 'C': 'math', 'D': 'x', 'E': 'abc',
                'F': 'pow', 'G': 'sin', 'H': 'cos', 'I': 'tan', 'J': 'ln',
                'K': 'log', 'L': 'sqr', 'M': 'sign', 'N': 'paren', 'O': ',',
                'P': 'exp', 'Q': '7', 'R': '8', 'S': '9', 'T': '/',
                'U': '4', 'V': '5', 'W': '6', 'X': '*', 'Y': '1', 'Z': '2'
            }
            
            SYMBOL_MAP = {
                '<': ['shift', '6', '7'],
                '>': ['shift', '6', '9'],
                '=': ['shift', '.'],
                '!': ['shift', '9', 'enter'],
                "'": ['shift', '9', 'right', 'right', 'right', 'right', 'enter'],
                '&': ['shift', '9', 'right', 'right', 'right', 'right', 'right', 'right', 'enter'],
                '+': ['+'],
                '-': ['-'],
                '*': ['*'],
                '/': ['/'],
                ',': [','],
                '.': ['.'],
                ';': ['alpha', '+'],
                ':': ['alpha', '-'],
                '^': ['pow'],
                '_': ['shift', 'space'],
            }
            
            # Convert text to sequence of keys with auto-closing bracket tracking
            keys_to_press = []
            quote_open = False
            paren_level = 0
            brace_open = False
            bracket_open = False
            
            for char in text:
                if char == '\n':
                    keys_to_press.append('enter')
                elif char == '\r':
                    continue
                elif char == '\t':
                    keys_to_press.append('right')
                elif char == ' ':
                    keys_to_press.append('space')
                elif char in '0123456789':
                    keys_to_press.append(char)
                elif char.isalpha():
                    is_lower = char.islower()
                    char_upper = char.upper()
                    key_name = CHAR_TO_KEY.get(char_upper)
                    
                    if mode == 'PPL':
                        if is_lower:
                            # Lowercase in PPL: alpha + shift + key
                            keys_to_press.extend(['alpha', 'shift', key_name])
                        else:
                            # Uppercase in PPL: alpha + key
                            keys_to_press.extend(['alpha', key_name])
                    else: # CAS or PYTHON
                        if is_lower:
                            # Lowercase in CAS/Python: alpha + key
                            keys_to_press.extend(['alpha', key_name])
                        else:
                            # Uppercase in CAS/Python: alpha + shift + key
                            keys_to_press.extend(['alpha', 'shift', key_name])
                elif char == '"':
                    if not quote_open:
                        keys_to_press.extend(['alpha', '0'])
                        quote_open = True
                    else:
                        keys_to_press.append('right')
                        quote_open = False
                elif char == '(':
                    keys_to_press.append('paren')
                    paren_level += 1
                elif char == ')':
                    if paren_level > 0:
                        keys_to_press.append('right')
                        paren_level -= 1
                    else:
                        keys_to_press.append('right')
                elif char == '{':
                    keys_to_press.extend(['shift', '8'])
                    brace_open = True
                elif char == '}':
                    if brace_open:
                        keys_to_press.append('right')
                        brace_open = False
                    else:
                        keys_to_press.append('right')
                elif char == '[':
                    keys_to_press.extend(['shift', '5'])
                    bracket_open = True
                elif char == ']':
                    if bracket_open:
                        keys_to_press.append('right')
                        bracket_open = False
                    else:
                        keys_to_press.append('right')
                elif char in SYMBOL_MAP:
                    keys_to_press.extend(SYMBOL_MAP[char])
                else:
                    return {
                        'jsonrpc': '2.0',
                        'id': req_id,
                        'result': {
                            'content': [{'type': 'text', 'text': f"ERROR: Character '{char}' is not supported."}],
                            'isError': True
                        }
                    }
                    
            for key in keys_to_press:
                keycode = KEY_MAPPING[key]
                send_cmd(f"key {keycode} press")
                time.sleep(delay_ms / 1000.0)
                
            return {
                'jsonrpc': '2.0',
                'id': req_id,
                'result': {
                    'content': [{'type': 'text', 'text': f"OK (Typed string '{text}')"}],
                    'isError': False
                }
            }
            
        elif tool_name == 'write_hpprgm':
            filename = args.get('filename', '')
            content = args.get('content', '')
            try:
                import struct, os
                header = bytearray([0x0C, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])
                filename_utf16 = filename.encode('utf-16-le') + b'\x00\x00'
                
                header_size = 14 + len(filename_utf16)
                header[0:4] = struct.pack('<I', header_size)
                header[8] = 1
                
                # Append 0x0031
                header.extend(struct.pack('<H', 0x0031))
                header.extend(filename_utf16)
                
                source_code = content.encode('utf-16-le') + b'\x00\x00'
                header.extend(struct.pack('<I', len(source_code)))
                header.extend(source_code)
                
                base_dir = os.path.dirname(os.path.abspath(__file__))
                data_dir = os.path.join(base_dir, "x64", "Release", "prime_data", "C", "DATA")
                os.makedirs(data_dir, exist_ok=True)
                out_path = os.path.join(data_dir, f"{filename}.hpprgm")
                with open(out_path, "wb") as f:
                    f.write(header)
                    
                return {
                    'jsonrpc': '2.0',
                    'id': req_id,
                    'result': {
                        'content': [{'type': 'text', 'text': f"Successfully wrote {filename}.hpprgm. You must 'Check' it inside the emulator to upgrade the legacy format."}],
                        'isError': False
                    }
                }
            except Exception as e:
                return {
                    'jsonrpc': '2.0',
                    'id': req_id,
                    'result': {
                        'content': [{'type': 'text', 'text': f"ERROR: {str(e)}"}],
                        'isError': True
                    }
                }
                
        elif tool_name == 'create_hpappdir':
            app_name = args.get('app_name', '')
            try:
                import os, shutil
                base_dir = os.path.dirname(os.path.abspath(__file__))
                data_dir = os.path.join(base_dir, "x64", "Release", "prime_data", "C", "DATA")
                
                src_app_dir = os.path.join(data_dir, "&Function.hpappdir")
                src_app_file = os.path.join(src_app_dir, "&Function.hpapp")
                
                dest_app_dir = os.path.join(data_dir, f"{app_name}.hpappdir")
                dest_app_file = os.path.join(dest_app_dir, f"{app_name}.hpapp")
                
                if not os.path.exists(src_app_file):
                    raise Exception(f"Source base app file not found at {src_app_file}")
                    
                os.makedirs(dest_app_dir, exist_ok=True)
                shutil.copy2(src_app_file, dest_app_file)
                
                return {
                    'jsonrpc': '2.0',
                    'id': req_id,
                    'result': {
                        'content': [{'type': 'text', 'text': f"Successfully created blank app template at {app_name}.hpappdir"}],
                        'isError': False
                    }
                }
            except Exception as e:
                return {
                    'jsonrpc': '2.0',
                    'id': req_id,
                    'result': {
                        'content': [{'type': 'text', 'text': f"ERROR: {str(e)}"}],
                        'isError': True
                    }
                }
                
        else:
            return {
                'jsonrpc': '2.0',
                'id': req_id,
                'error': {
                    'code': -32601,
                    'message': f"Method not found: {tool_name}"
                }
            }
            
    return None

def main():
    # Force stdin/stdout to work in binary mode or ensure utf-8 encoding
    sys.stdout.reconfigure(encoding='utf-8')
    sys.stdin.reconfigure(encoding='utf-8')
    
    for line in sys.stdin:
        if not line.strip():
            continue
        try:
            req = json.loads(line)
            res = handle_request(req)
            if res:
                sys.stdout.write(json.dumps(res) + '\n')
                sys.stdout.flush()
        except Exception as e:
            # Send error JSON-RPC response if we parsed enough to get request ID
            err_res = {
                'jsonrpc': '2.0',
                'error': {
                    'code': -32603,
                    'message': f"Internal error: {e}"
                }
            }
            sys.stdout.write(json.dumps(err_res) + '\n')
            sys.stdout.flush()

if __name__ == '__main__':
    main()
