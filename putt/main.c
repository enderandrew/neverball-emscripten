/*
 * Copyright (C) 2003 Robert Kooima
 *
 * NEVERPUTT is  free software; you can redistribute  it and/or modify
 * it under the  terms of the GNU General  Public License as published
 * by the Free  Software Foundation; either version 2  of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT  ANY  WARRANTY;  without   even  the  implied  warranty  of
 * MERCHANTABILITY or  FITNESS FOR A PARTICULAR PURPOSE.   See the GNU
 * General Public License for more details.
 */

/*---------------------------------------------------------------------------*/

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif					 
#include <SDL.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

#include "version.h"
#include "glext.h"
#include "audio.h"
#include "image.h"
#include "state.h"
#include "config.h"
#include "video.h"
#include "text.h"
#include "mtrl.h"
#include "geom.h"
#include "course.h"
#include "hole.h"
#include "game.h"
#include "gui.h"
#include "hmd.h"
#include "fs.h"
#include "joy.h"
#include "log.h"

#include "st_conf.h"
#include "st_all.h"

int camera = 0;

const char TITLE[] = "Neverputt";
const char ICON[] = "icon/neverputt.png";

/*---------------------------------------------------------------------------*/

static void shot(void)
{
    static char filename[MAXSTR];
    sprintf(filename, "Screenshots/screen%05d.png", config_screenshot());
    video_snap(filename);
}

/*---------------------------------------------------------------------------*/

static void toggle_wire(void)
{
#if !ENABLE_OPENGLES
    static int wire = 0;

    if (wire)
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glEnable(GL_TEXTURE_2D);
        wire = 0;
    }
    else
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glDisable(GL_TEXTURE_2D);
        wire = 1;
    }
#endif
}

/*---------------------------------------------------------------------------*/

/*
 * Track held direction keys.
 */
static char key_pressed[4];

static const int key_other[4] = { 1, 0, 3, 2 };

static const int *key_axis[4] = {
    &CONFIG_JOYSTICK_AXIS_Y0,
    &CONFIG_JOYSTICK_AXIS_Y0,
    &CONFIG_JOYSTICK_AXIS_X0,
    &CONFIG_JOYSTICK_AXIS_X0
};

static const float key_tilt[4] = { -1.0f, +1.0f, -1.0f, +1.0f };

static int handle_key_dn(SDL_Event *e)
{
    int d = 1;
    int c = e->key.keysym.sym;

    int dir = -1;

    /* SDL made me do it. */
#ifdef __APPLE__
    if (c == SDLK_q && e->key.keysym.mod & KMOD_GUI)
        return 0;
#endif
#ifdef _WIN32
    if (c == SDLK_F4 && e->key.keysym.mod & KMOD_ALT)
        return 0;
#endif

    switch (c)
    {
    case KEY_SCREENSHOT:
        shot();
        break;
    case KEY_FPS:
        config_tgl_d(CONFIG_FPS);
        break;
    case KEY_WIREFRAME:
        if (config_cheat())
            toggle_wire();
        break;
    case KEY_RESOURCES:
        if (config_cheat())
        {
            light_load();
            mtrl_reload();
        }
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        d = st_buttn(config_get_d(CONFIG_JOYSTICK_BUTTON_A), 1);
        break;
    case KEY_FULLSCREEN:
        video_fullscreen(!config_get_d(CONFIG_FULLSCREEN));
        break;
    case KEY_EXIT:
        d = st_keybd(KEY_EXIT, 1);
        break;

    default:
        if (config_tst_d(CONFIG_KEY_FORWARD,  c))
            dir = 0;
        else if (config_tst_d(CONFIG_KEY_BACKWARD, c))
            dir = 1;
        else if (config_tst_d(CONFIG_KEY_LEFT, c))
            dir = 2;
        else if (config_tst_d(CONFIG_KEY_RIGHT, c))
            dir = 3;

        if (dir != -1)
        {
            /* Ignore auto-repeat on direction keys. */

            if (e->key.repeat)
                break;

            key_pressed[dir] = 1;
            st_stick(config_get_d(*key_axis[dir]), key_tilt[dir]);
        }
        else
            d = st_keybd(e->key.keysym.sym, 1);
    }

    return d;
}

static int handle_key_up(SDL_Event *e)
{
    int d = 1;
    int c = e->key.keysym.sym;

    int dir = -1;

    switch (c)
    {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        d = st_buttn(config_get_d(CONFIG_JOYSTICK_BUTTON_A), 0);
        break;
    case KEY_EXIT:
        d = st_keybd(KEY_EXIT, 0);
        break;
    default:
        if (config_tst_d(CONFIG_KEY_FORWARD, c))
            dir = 0;
        else if (config_tst_d(CONFIG_KEY_BACKWARD, c))
            dir = 1;
        else if (config_tst_d(CONFIG_KEY_LEFT, c))
            dir = 2;
        else if (config_tst_d(CONFIG_KEY_RIGHT, c))
            dir = 3;

        if (dir != -1)
        {
            key_pressed[dir] = 0;

            if (key_pressed[key_other[dir]])
                st_stick(config_get_d(*key_axis[dir]), -key_tilt[dir]);
            else
                st_stick(config_get_d(*key_axis[dir]), 0.0f);
        }
        else
            d = st_keybd(e->key.keysym.sym, 0);
    }

    return d;
}

#ifdef __EMSCRIPTEN__

enum {
    USER_EVENT_BACK = -1,
    USER_EVENT_PAUSE = 0
};

void push_user_event(int code)
{
    SDL_Event event = { SDL_USEREVENT };
    event.user.code = code;
    SDL_PushEvent(&event);
}
#endif


/*---------------------------------------------------------------------------*/

static int loop(void)
{
    SDL_Event e;
    int d = 1;
    int c;

    int ax, ay, dx, dy;
	
#ifdef __EMSCRIPTEN__
    /* Since we are in the browser, and want to look good on every device,
     * naturally, we use CSS to do layout. The canvas element has two sizes:
     * the layout size ("window") and the drawing buffer size ("resolution").
     * Here, we get the canvas layout size and set the canvas resolution
     * to match. To update a bunch of internal state, we use SDL_SetWindowSize
     * to set the canvas resolution.
     */

    double clientWidth, clientHeight;

    int w, h;

    emscripten_get_element_css_size("#canvas", &clientWidth, &clientHeight);

    w = (int) clientWidth;
    h = (int) clientHeight;

    if (w != video.window_w || h != video.window_h)
        video_set_window_size(w, h);
#endif

    /* Process SDL events. */

    while (d && SDL_PollEvent(&e))
    {
        switch (e.type)
        {
        case SDL_QUIT:
            return 0;

#ifdef __EMSCRIPTEN__
        case SDL_USEREVENT:
            switch (e.user.code)
            {
                case USER_EVENT_BACK:
                    d = st_keybd(KEY_EXIT, 1);
                    break;

                case USER_EVENT_PAUSE:
                    if (video_get_grab())
                        goto_state(&st_pause);
                    break;
            }
            break;
#endif

        case SDL_MOUSEMOTION:
            /* Convert to OpenGL coordinates. */

            ax = +e.motion.x;
            ay = -e.motion.y + video.window_h;
            dx = +e.motion.xrel;
            dy = -e.motion.yrel;

            /* Convert to pixels. */

            ax = ROUND(ax * video.device_scale);
            ay = ROUND(ay * video.device_scale);
            dx = ROUND(dx * video.device_scale);
            dy = ROUND(dy * video.device_scale);

            st_point(ax, ay, dx, dy);

            break;

        case SDL_MOUSEBUTTONDOWN:
            d = st_click(e.button.button, 1);
            break;

        case SDL_MOUSEBUTTONUP:
            d = st_click(e.button.button, 0);
            break;

        case SDL_FINGERDOWN:
        case SDL_FINGERUP:
        case SDL_FINGERMOTION:
            d = st_touch(&e.tfinger);
            break;

        case SDL_KEYDOWN:

            c = e.key.keysym.sym;

        case SDL_KEYUP:

            c = e.key.keysym.sym;

            switch (c)
            {
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                d = st_buttn(config_get_d(CONFIG_JOYSTICK_BUTTON_A), 0);
                break;
            case SDLK_ESCAPE:
                if (video_get_grab())
                    d = st_buttn(config_get_d(CONFIG_JOYSTICK_BUTTON_START), 0);
                else
                    d = st_buttn(config_get_d(CONFIG_JOYSTICK_BUTTON_B), 0);
                break;

            default:
                if (config_tst_d(CONFIG_KEY_FORWARD, c))
                    st_stick(config_get_d(CONFIG_JOYSTICK_AXIS_Y0), 0.0f);
                else if (config_tst_d(CONFIG_KEY_BACKWARD, c))
                    st_stick(config_get_d(CONFIG_JOYSTICK_AXIS_Y0), 0.0f);
                else if (config_tst_d(CONFIG_KEY_LEFT, c))
                    st_stick(config_get_d(CONFIG_JOYSTICK_AXIS_X0), 0.0f);
                else if (config_tst_d(CONFIG_KEY_RIGHT, c))
                    st_stick(config_get_d(CONFIG_JOYSTICK_AXIS_X0), 0.0f);
                else
                    d = st_keybd(e.key.keysym.sym, 0);
            }
            break;

        case SDL_WINDOWEVENT:
            switch (e.window.event)
            {
            case SDL_WINDOWEVENT_FOCUS_LOST:
                if (video_get_grab())
                    goto_pause(&st_over);
                break;

            case SDL_WINDOWEVENT_MOVED:
                if (config_get_d(CONFIG_DISPLAY) != video_display())
                    config_set_d(CONFIG_DISPLAY, video_display());
                break;

            case SDL_WINDOWEVENT_SIZE_CHANGED:
                video_resize(e.window.data1, e.window.data2);
                gui_resize();
                break;
            }
            break;

        case SDL_TEXTINPUT:
            text_input_str(e.text.text, 1);
            break;

        case SDL_JOYAXISMOTION:
            joy_axis(e.jaxis.which, e.jaxis.axis, JOY_VALUE(e.jaxis.value));
            break;

        case SDL_JOYBUTTONDOWN:
            d = joy_button(e.jbutton.which, e.jbutton.button, 1);
            break;

        case SDL_JOYBUTTONUP:
            d = joy_button(e.jbutton.which, e.jbutton.button, 0);
            break;

        case SDL_JOYDEVICEADDED:
            joy_add(e.jdevice.which);
            break;

        case SDL_JOYDEVICEREMOVED:
            joy_remove(e.jdevice.which);
            break;

        case SDL_MOUSEWHEEL:
            st_wheel(e.wheel.x, e.wheel.y);
            break;
        }
    }

    return d;
}

/*---------------------------------------------------------------------------*/

static char *opt_data;
static char *opt_hole;

static void opt_parse(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--data") == 0)
        {
            if (++i < argc)
                opt_data = argv[i];
        }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--hole") == 0)
        {
            if (++i < argc)
                opt_hole = argv[i];
        }
    }

    if (argc == 2)
    {
        size_t len = strlen(argv[1]);

        if (len > 4)
        {
            char *ext = argv[1] + len - 4;

            if (strcmp(ext, ".map") == 0)
                strcpy(ext, ".sol");

            if (strcmp(ext, ".sol") == 0)
                opt_hole = argv[1];
        }
    }
}

/*---------------------------------------------------------------------------*/

static void main_quit(void);

struct main_loop
{
    Uint32 now;
    unsigned int done:1;
};

static void step(void *data)
{
    struct main_loop *mainloop = (struct main_loop *) data;

    int running = loop();

    if (running)
    {
        Uint32 now = SDL_GetTicks();
        Uint32 dt = (now - mainloop->now);

        if (0 < dt && dt < 1000)
        {
            /* Step the game state. */

            st_timer(0.001f * dt);

            /* Render. */

            hmd_step();
            st_paint(0.001f * now);
            video_swap();
        }

        mainloop->now = now;
    }

    mainloop->done = !running;

#ifdef __EMSCRIPTEN__
    /* On Emscripten, we never return to main(), so we have to do shutdown here. */

    if (mainloop->done)
    {
        emscripten_cancel_main_loop();
        main_quit();

        EM_ASM({
            Neverputt.quit();
        });
    }
#endif
}

/*
 * Initialize all systems.
 */
static int main_init(int argc, char *argv[])
{
    if (!fs_init(argc > 0 ? argv[0] : NULL))
    {
        fprintf(stderr, "Failure to initialize virtual file system (%s)\n",
                fs_error());
        return 1;
    }

    srand((int) time(NULL));

    opt_parse(argc, argv);

    config_paths(opt_data);
    log_init("Neverputt" VERSION, "neverputt.log");
    fs_mkdir("Screenshots");

    /* Initialize SDL. */

#ifdef SDL_HINT_TOUCH_MOUSE_EVENTS
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) == -1)
    {
        log_printf("Failure to initialize SDL (%s)\n", SDL_GetError());
        return 0;
    }

    /* Enable joystick events. */

    joy_init();

    /* Intitialize configuration. */

    config_init();
    config_load();

    /* Initialize localization. */

    lang_init();
	
    /* Cache Neverputt's camera setting. */
	
    camera = config_get_d(CONFIG_CAMERA);
	
    /* Initialize the audio. */
	
    audio_init();
	
    /* Initialize the video. */
    if (!video_init())
        return 0;

    /* Material system. */

    mtrl_init();

    return 1;
}

/*
 * Shut down all systems.
 */
static void main_quit(void)
{
    config_save();

    /* Free everything else. */

    goto_state(&st_null);

    /* Restore Neverputt's camera setting. */

    config_set_d(CONFIG_CAMERA, camera);
    config_save();

    mtrl_quit();
    video_quit();
    audio_free();
    lang_quit();
    joy_quit();
    config_quit();
    SDL_Quit();
    log_quit();
    fs_quit();
}
	
int main(int argc, char *argv[])	
{
    struct main_loop mainloop = { 0 };

    struct state *start_state = &st_title;

    if (!main_init(argc, argv))
        return 1;

    /* Screen states. */

    init_state(&st_null);
	
    if (opt_hole)
    {
        const char *path = fs_resolve(opt_hole);
        int loaded = 0;

        if (path)
        {
            hole_init(NULL);

            if (hole_load(0, path) &&
                hole_load(1, path) &&
                hole_goto(1, 1))
            {
                goto_state(&st_next);
                loaded = 1;
            }
        }

        if (!loaded)
            goto_state(&st_title);
    }
    else
        goto_state(&st_title);

    /* Run the main game loop. */

    mainloop.now = SDL_GetTicks();

#ifdef __EMSCRIPTEN__
    /*
     * The Emscripten main loop is asynchronous. In other words,
     * emscripten_set_main_loop_arg() returns immediately. The fourth
     * parameter basically just determines what happens with main()
     * beyond this point:
     *
     *   0 = execution continues to the end of main().
     *   1 = execution stops here, the rest of main() is never executed.
     *
     * It's best not to put anything after this.
     */
    emscripten_set_main_loop_arg(step, (void *) &mainloop, 0, 1);
#else
    while (!mainloop.done)
        step(&mainloop);

    main_quit();
#endif

    return 0;
}

/*---------------------------------------------------------------------------*/
