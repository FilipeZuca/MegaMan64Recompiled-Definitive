# Lists controllers as the game's SDL2 sees them (name and SDL_GameControllerType).
import ctypes, sys
sdl = ctypes.CDLL(sys.argv[1])
sdl.SDL_GameControllerNameForIndex.restype = ctypes.c_char_p
sdl.SDL_JoystickNameForIndex.restype = ctypes.c_char_p
sdl.SDL_SetHint(b"SDL_JOYSTICK_HIDAPI_PS5", b"1")
assert sdl.SDL_Init(0x2000 | 0x200) == 0  # GAMECONTROLLER | JOYSTICK
types = ['UNKNOWN', 'XBOX360', 'XBOXONE', 'PS3', 'PS4', 'NINTENDO_SWITCH_PRO', 'VIRTUAL', 'PS5', 'AMAZON_LUNA', 'GOOGLE_STADIA',
         'NVIDIA_SHIELD', 'SWITCH_JOYCON_LEFT', 'SWITCH_JOYCON_RIGHT', 'SWITCH_JOYCON_PAIR']
n = sdl.SDL_NumJoysticks()
print('joysticks:', n)
for i in range(n):
    t = sdl.SDL_GameControllerTypeForIndex(i)
    print(i, sdl.SDL_JoystickNameForIndex(i), 'gamecontroller=%d' % sdl.SDL_IsGameController(i), 'type=%s' % (types[t] if t < len(types) else t))
