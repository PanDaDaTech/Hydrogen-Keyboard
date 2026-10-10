# -*- coding: utf-8 -*-
import re
src = open('hkeyboard.cpp', encoding='utf-8').read()
# strip comments and strings to reduce noise
s = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
s = re.sub(r'//[^\n]*', ' ', s)
s = re.sub(r'L?"(?:\\.|[^"\\])*"', '""', s)
s = re.sub(r"'(?:\\.|[^'\\])+'", "''", s)

defined = set()
# static/non-static function definitions & forward decls
for m in re.finditer(r'\b(\w+)\s*\([^;{}]*\)\s*(?:const\s*)?\{', s):
    defined.add(m.group(1))
for m in re.finditer(r'\b(\w+)\s*\([^;{}]*\)\s*;', s):
    defined.add(m.group(1))
# macros
for m in re.finditer(r'^\s*#\s*define\s+(\w+)', s, re.M):
    defined.add(m.group(1))
# class/struct member templates & typedef'd fn ptr types
for m in re.finditer(r'\btypedef\b[^;]*?(\w+)\s*;', s):
    defined.add(m.group(1))
# local variables that could be called (fn pointers) - collect all declarator names
for m in re.finditer(r'\b(\w+)\s*(?:=|;|,|\))', s):
    pass

calls = {}
for m in re.finditer(r'(?<![\w.>])([A-Za-z_]\w*)\s*\(', s):
    nm = m.group(1)
    line = s.count('\n', 0, m.start()) + 1
    calls.setdefault(nm, []).append(line)

KW = {'if','for','while','switch','return','sizeof','catch','throw','new','delete','defined',
      'static_cast','reinterpret_cast','const_cast','dynamic_cast','else','do','case','and','or','not'}
cand = []
for nm, ls in calls.items():
    if nm in defined or nm in KW: continue
    cand.append((nm, ls))

# heuristics: things that are likely Win32 APIs start with known prefixes or are CamelCase with common verbs
WIN = re.compile(r'^(Get|Set|Create|Destroy|Delete|Close|Open|Read|Write|Send|Post|Show|Hide|Move|Begin|End|Register|Unregister|Load|Free|Alloc|Lock|Unlock|Invalidate|Update|Draw|Fill|Text|DrawText|Select|Enum|Query|Is|Enable|Disable|Set|Adjust|Map|Unmap|Screen|System|Win|Msg|Scroll|Client|Kill|Sleep|Wait|Multi|Var|Safe|Global|Local|Heap|Virtual|Copy|Compare|Char|Wide|Wcs|Str|Mem|Time|Tls|Init|Deactivate|Release|Add|Remove|Notify|Shell|Drag|Track|Cell|Image|Create|GetSys|Open|Close|Combine|Insert|Remove|Rect|Pt|Pow|Alpha|Ellipse|Rectangle|Round|Polygon|Line|Arc|Chord|Pie|Flood|Ext|BitBlt|Stretch|Mask|Pat|CreateDIBSection|CreateCompatible|CreateSolid|CreatePen|CreateFont|CreateBrush|DeleteObject|DeleteDC|GetObject|SetBk|SetText|SetBkMode|SetStretch|GetText|GetStock|GetDeviceCaps|GetModule|GetCurrent|GetLast|GetTick|GetCursor|GetKeyState|GetFocus|GetForeground|GetWindow|GetClient|GetDC|ReleaseDC|FillRect|FrameRect|MoveToEx|LineTo|L"|W"|Shell_NotifyIcon|ExtractIcon|DrawIcon|LoadImage|CreateWindow|DefWindowProc|RegisterClass|PeekMessage|DispatchMessage|TranslateMessage|GetMessage|PostQuitMessage|CreateSolidBrush|EnumFont|AddFontMem|RemoveFontMem|GetSysColor|SetWindowPos|SetWindowLong|GetWindowLong|SetLayered|UpdateLayered|AlphaBlend|GradientFill|TransparentBlt|SetDIBits|GetDIBits|CreateDIBSection|OpenProcess|CloseHandle|WaitForSingleObject|CreateThread|RegOpen|RegClose|RegSet|RegQuery|RegDelete|ShellExecute|SHGet|PathFind|ExtractIcon|GetSystemMetrics|SystemParametersInfo|MonitorFrom|GetMonitor|EnumDisplay|ChangeDisplay|SystemParametersInfo|SetProcess|SetThread|GetThread|LoadLibrary|GetProcAddress|FreeLibrary|MessageBox|wsprintf|swprintf|wcs|str|mem|malloc|free|calloc|realloc|abs|atoi|_wtoi|sprintf|printf|CreateMutex|ReleaseMutex|GetEnvironment|ExpandEnvironment|GetPrivateProfile|WritePrivateProfile|GetFullPath|CreateDirectory|GetFileAttributes|FindFirst|FindNext|CreateFile|GetConsole)$')
unknown = [c for c in cand if not WIN.match(c[0])]
unknown.sort()
print('total call-like names:', len(calls), '| not defined in file:', len(cand), '| unmatched by Win32 heuristic:', len(unknown))
print()
for nm, ls in unknown:
    print(f'  {nm:38s} 行 {ls[:6]}')
