#include <chrono>
#include <cstring>
#include <iostream>
#include <ranges>
#include <vector>
#include <oneapi/tbb/profiling.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

constexpr xcb_keycode_t KEYCODE_RETURN = 36;
constexpr xcb_keycode_t KEYCODE_ESCAPE = 9;
constexpr xcb_keycode_t KEYCODE_Q = 24;
constexpr xcb_keycode_t KEYCODE_W = 25;
constexpr xcb_keycode_t KEYCODE_J = 44;
constexpr xcb_keycode_t KEYCODE_K = 45;
constexpr xcb_keycode_t KEYCODE_D = 40;
constexpr xcb_keycode_t KEYCODE_PLUS = 21;
constexpr xcb_keycode_t KEYCODE_MINUS = 20;
int gap_size = 20;

xcb_window_t focused_client_window = XCB_WINDOW_NONE;
std::vector<xcb_window_t> client_windows;
uint32_t focused_border;
uint32_t unfocused_border;


void spawn(const char* command) {
    if (fork() == 0) {
        setsid();
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        execlp(command, command, NULL);
        _exit(127);
    }
}

void focus_client(xcb_connection_t* conn, xcb_window_t window_id) {
    static xcb_window_t last_focused = XCB_WINDOW_NONE;
    if (focused_client_window == window_id) return;


    if (last_focused != XCB_WINDOW_NONE && last_focused != window_id) {
        std::cout << "  Changing border of previous focused window " << last_focused << " to unfocused color." << std::endl;
        xcb_change_window_attributes(conn, last_focused, XCB_CW_BORDER_PIXEL, &unfocused_border);
    }

    if (window_id != XCB_WINDOW_NONE) {
        xcb_change_window_attributes(conn, window_id, XCB_CW_BORDER_PIXEL, &focused_border);
        last_focused = window_id;
        std::cout << "Focusing client " << window_id << std::endl;
        focused_client_window = window_id;
        xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, window_id, XCB_CURRENT_TIME);
        xcb_circulate_window(conn, XCB_CIRCULATE_RAISE_LOWEST, window_id);
    } else {
        focused_client_window = XCB_WINDOW_NONE;
    }

    xcb_flush(conn);
}

void kill_client(xcb_connection_t* conn, xcb_window_t window_id) {
    if (window_id == XCB_WINDOW_NONE) {
        std::cerr << "kill_client called with XCB_WINDOW_NONE. Aborting." << std::endl;
        return;
    }
    std::cout << "Attempting to kill " << window_id << std::endl;
    xcb_intern_atom_cookie_t wm_delete_window_cookie = xcb_intern_atom(conn, 0, strlen("WM_DELETE_WINDOW"), "WM_DELETE_WINDOW");
    xcb_intern_atom_reply_t* wm_delete_window_reply = xcb_intern_atom_reply(conn, wm_delete_window_cookie, nullptr);
    xcb_intern_atom_cookie_t wm_protocols_cookie = xcb_intern_atom(conn, 0, strlen("WM_PROTOCOLS"), "WM_PROTOCOLS");
    xcb_intern_atom_reply_t* wm_protocols_reply = xcb_intern_atom_reply(conn, wm_protocols_cookie, nullptr);
    if (wm_delete_window_reply && wm_protocols_reply) {
        xcb_client_message_event_t event;
        event.response_type = XCB_CLIENT_MESSAGE;
        event.format = 32;
        event.sequence = 0;
        event.window = window_id;
        event.type = wm_protocols_reply -> atom;
        event.data.data32[0] = wm_delete_window_reply -> atom;
        event.data.data32[1] = XCB_CURRENT_TIME;
        xcb_send_event(conn, 0, window_id, XCB_EVENT_MASK_NO_EVENT, (const char*)&event);
        xcb_flush(conn);
    }
    else {
        std::cerr << "Could not send WM_DELETE_WINDOW, forcefully killing client." << std::endl;
        xcb_kill_client(conn, window_id);
        xcb_flush(conn);
    }
    free(wm_delete_window_reply);
    free(wm_protocols_reply);
}

void ungrab_key_with_mods(xcb_connection_t* conn, xcb_window_t root, xcb_keycode_t keycode, uint16_t modifiers) {
    uint16_t num_lock_mask = XCB_MOD_MASK_2;
    uint16_t caps_lock_mask = XCB_MOD_MASK_LOCK;
    xcb_ungrab_key(conn, keycode, modifiers, root);
    xcb_ungrab_key(conn, keycode, modifiers | caps_lock_mask, root);
    xcb_ungrab_key(conn, keycode, modifiers | num_lock_mask, root);
    xcb_ungrab_key(conn, keycode, modifiers | num_lock_mask | caps_lock_mask, root);
}

uint32_t get_color_pixel(xcb_connection_t* conn, xcb_screen_t* screen, uint16_t red, uint16_t green, uint16_t blue) {
    xcb_alloc_color_cookie_t color_cookie = xcb_alloc_color(conn, screen->default_colormap, red, green, blue);
    xcb_alloc_color_reply_t* color_reply = xcb_alloc_color_reply(conn, color_cookie, NULL);
    if (!color_reply) return screen->black_pixel;
    uint32_t pixel = color_reply->pixel;
    free(color_reply);
    return pixel;
}

void apply_master_stack(xcb_connection_t* connection, xcb_screen_t* screen) {
    if (client_windows.empty()) return;

    if (client_windows.size() == 1) {
        uint32_t fullscreen_geom[4] = {
            gap_size, // x
            gap_size, // y
            (uint32_t)(screen->width_in_pixels - 2 * gap_size),
            (uint32_t)(screen->height_in_pixels - 2 * gap_size)
        };
        xcb_configure_window(connection, client_windows[0], XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, fullscreen_geom);
        xcb_flush(connection);
        return;
    }
    xcb_window_t master = client_windows[0];
    int usable_width = screen->width_in_pixels - 2 * gap_size;
    int usable_height = screen->height_in_pixels - 2 * gap_size;
    int master_width = (usable_width * 0.6) - (gap_size / 2);
    int stack_width = usable_width - master_width - gap_size;
    int stack_count = client_windows.size() - 1;

    uint32_t master_geom[4] = {
        gap_size,
        gap_size,
        (uint32_t)master_width,
        (uint32_t)usable_height
    };
    xcb_configure_window(connection, master,
        XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
        master_geom);

    for (size_t i = 1; i < client_windows.size(); ++i) {
        int stack_height_per_window = (usable_height - (stack_count - 1) * gap_size) / stack_count;
        int stack_y = gap_size + (i-1) * (stack_height_per_window + gap_size);
        uint32_t stack_geom[4] = {
            gap_size + master_width + gap_size,
            (uint32_t)stack_y,
            (uint32_t)stack_width,
            (uint32_t)stack_height_per_window
        };
        xcb_configure_window(connection, client_windows[i],
            XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
            stack_geom);
    }
    xcb_flush(connection);
}

int main() {
    xcb_connection_t* connection;
    xcb_screen_t* screen;
    xcb_generic_event_t* event = nullptr;

    connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(connection)) {
        std::cerr << "Failed to connect to X server" << std::endl;
        return 1;
    }
    std::cout << "Connected to X server" << std::endl;

    const xcb_setup_t* setup = xcb_get_setup(connection);
    screen = xcb_setup_roots_iterator(setup).data;
    focused_border = get_color_pixel(connection, screen, 65535, 42405, 0);
    unfocused_border = get_color_pixel(connection, screen, 30000, 30000, 30000);

    if (screen) {
        std::cout << screen->width_in_pixels << "x" << screen->height_in_pixels << std::endl;
        uint32_t mask;
        mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
               XCB_EVENT_MASK_STRUCTURE_NOTIFY |
               XCB_EVENT_MASK_KEY_PRESS |
               XCB_EVENT_MASK_FOCUS_CHANGE |
               XCB_EVENT_MASK_BUTTON_PRESS |
               XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;

        xcb_change_window_attributes(connection, screen->root, XCB_CW_EVENT_MASK, &mask);
        xcb_generic_error_t *error = xcb_request_check(connection, xcb_change_window_attributes_checked(connection, screen->root, XCB_CW_EVENT_MASK, &mask));
        if (error) {
            std::cerr << "Error setting event mask (another WM likely running): " << "Error code " << (int)error->error_code << ", Minor code " << (int)error->minor_code << std::endl;
            free(error);
            xcb_disconnect(connection);
            return 1;
        }

        uint16_t modmask_super = XCB_MOD_MASK_4;
        uint16_t num_lock_mask = XCB_MOD_MASK_2;
        uint16_t caps_lock_mask = XCB_MOD_MASK_LOCK;

        auto grab_key_with_mods = [&](xcb_keycode_t keycode, uint16_t modifiers) {
            xcb_grab_key(connection, 1, screen->root, modifiers, keycode, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
            xcb_grab_key(connection, 1, screen->root, modifiers | num_lock_mask, keycode, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
            xcb_grab_key(connection, 1, screen->root, modifiers | caps_lock_mask, keycode, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
            xcb_grab_key(connection, 1, screen->root, modifiers | num_lock_mask | caps_lock_mask, keycode, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
        };

        grab_key_with_mods(KEYCODE_RETURN, modmask_super);
        grab_key_with_mods(KEYCODE_ESCAPE, modmask_super);
        grab_key_with_mods(KEYCODE_Q, modmask_super);
        grab_key_with_mods(KEYCODE_W, modmask_super);
        grab_key_with_mods(KEYCODE_J, modmask_super);
        grab_key_with_mods(KEYCODE_K, modmask_super);
        grab_key_with_mods(KEYCODE_D, modmask_super);
        grab_key_with_mods(KEYCODE_PLUS, modmask_super);
        grab_key_with_mods(KEYCODE_MINUS, modmask_super);
        xcb_flush(connection);

        while ((event = xcb_wait_for_event(connection))) {


            switch (event->response_type & ~0x80) {
                case XCB_MAP_REQUEST: {
                    auto* mr = (xcb_map_request_event_t*)event;
                    uint32_t values[4];
                    uint16_t mask_config = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
                    values[0] = 0;
                    values[1] = 0;
                    values[2] = screen->width_in_pixels;
                    values[3] = screen->height_in_pixels;
                    xcb_configure_window(connection, mr->window, mask_config, values);

                    uint32_t border_width = 2;
                    xcb_configure_window(connection, mr->window, XCB_CONFIG_WINDOW_BORDER_WIDTH, &border_width);

                    uint32_t client_mask = XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
                    xcb_change_window_attributes(connection, mr->window, XCB_CW_EVENT_MASK, &client_mask);

                    xcb_grab_button(connection, 0, mr->window,
                        XCB_EVENT_MASK_BUTTON_PRESS,
                        XCB_GRAB_MODE_SYNC,
                        XCB_GRAB_MODE_ASYNC,
                        XCB_WINDOW_NONE,
                        XCB_CURSOR_NONE,
                        XCB_BUTTON_INDEX_1,
                        XCB_MOD_MASK_ANY);

                    xcb_map_window(connection, mr->window);
                    xcb_change_window_attributes(connection, mr->window, XCB_CW_BORDER_PIXEL, &unfocused_border);

                    xcb_configure_notify_event_t configure_notify_event;
                    configure_notify_event.response_type = XCB_CONFIGURE_NOTIFY;
                    configure_notify_event.event = mr->window;
                    configure_notify_event.window = mr->window;
                    configure_notify_event.x = values[0];
                    configure_notify_event.y = values[1];
                    configure_notify_event.width = values[2];
                    configure_notify_event.height = values[3];
                    configure_notify_event.border_width = 0;
                    configure_notify_event.above_sibling = XCB_WINDOW_NONE;
                    configure_notify_event.override_redirect = false;
                    xcb_send_event(connection, 0, mr->window, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (const char*)&configure_notify_event);
                    xcb_flush(connection);

                    client_windows.push_back(mr->window);
                    apply_master_stack(connection, screen);

                    focus_client(connection, mr->window);
                    break;
                }

                case XCB_CONFIGURE_REQUEST: {
                    auto* cr = (xcb_configure_request_event_t*)event;
                    uint32_t values[4];
                    uint16_t mask_config = 0;
                    mask_config |= XCB_CONFIG_WINDOW_X;
                    values[0] = 0;
                    mask_config |= XCB_CONFIG_WINDOW_Y;
                    values[1] = 0;
                    mask_config |= XCB_CONFIG_WINDOW_WIDTH;
                    values[2] = screen->width_in_pixels;
                    mask_config |= XCB_CONFIG_WINDOW_HEIGHT;
                    values[3] = screen->height_in_pixels;
                    xcb_configure_window(connection, cr->window, mask_config, values);

                    xcb_configure_notify_event_t configure_notify_event;
                    configure_notify_event.response_type = XCB_CONFIGURE_NOTIFY;
                    configure_notify_event.event = cr->window;
                    configure_notify_event.window = cr->window;
                    configure_notify_event.x = values[0];
                    configure_notify_event.y = values[1];
                    configure_notify_event.width = values[2];
                    configure_notify_event.height = values[3];
                    configure_notify_event.border_width = 0;
                    configure_notify_event.above_sibling = XCB_WINDOW_NONE;
                    configure_notify_event.override_redirect = false;
                    xcb_send_event(connection, 0, cr->window, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (const char*)&configure_notify_event);

                    xcb_flush(connection);
                    break;
                }

                case XCB_DESTROY_NOTIFY: {
                    auto* dn = (xcb_destroy_notify_event_t*)event;
                    if (focused_client_window == dn->window) {
                        focused_client_window = XCB_WINDOW_NONE;
                    }
                    client_windows.erase(
                        std::remove(client_windows.begin(), client_windows.end(), dn->window),
                        client_windows.end());
                    apply_master_stack(connection, screen);

                    if (!client_windows.empty() && focused_client_window == XCB_WINDOW_NONE) {
                        focus_client(connection, client_windows.back());
                    }
                    break;
                }

                case XCB_KEY_PRESS: {
                    auto* kp = (xcb_key_press_event_t*)event;
                    uint16_t current_modmask = kp->state & (modmask_super | num_lock_mask | caps_lock_mask);

                    if ((kp->detail == KEYCODE_RETURN) && (current_modmask & modmask_super)) {
                        spawn("st");
                    }
                    else if ((kp->detail) == KEYCODE_D && (current_modmask & modmask_super)) {
                        spawn("dmenu_run");
                    }
                    else if ((kp->detail == KEYCODE_ESCAPE) && (current_modmask & modmask_super)) {
                        ungrab_key_with_mods(connection, screen->root, KEYCODE_RETURN, modmask_super);
                        ungrab_key_with_mods(connection, screen->root, KEYCODE_ESCAPE, modmask_super);
                        ungrab_key_with_mods(connection, screen->root, KEYCODE_D, modmask_super);
                        ungrab_key_with_mods(connection, screen->root, KEYCODE_Q, modmask_super);
                        ungrab_key_with_mods(connection, screen->root, KEYCODE_W, modmask_super);
                        ungrab_key_with_mods(connection, screen->root, KEYCODE_J, modmask_super);
                        ungrab_key_with_mods(connection, screen->root, KEYCODE_K, modmask_super);
                        uint32_t reset_mask = 0;
                        xcb_change_window_attributes(connection, screen->root, XCB_CW_EVENT_MASK, &reset_mask);
                        xcb_flush(connection);
                        free(event);
                        goto end_loop;
                    }
                    else if ((kp->detail == KEYCODE_Q) && (current_modmask & modmask_super)) {
                        if (focused_client_window != XCB_WINDOW_NONE && focused_client_window != screen->root) {
                            kill_client(connection, focused_client_window);
                        }
                    }
                    else if ((kp->detail == KEYCODE_J && (current_modmask & modmask_super))) {
                        if (!client_windows.empty()) {
                            auto it = std::find(client_windows.begin(), client_windows.end(), focused_client_window);
                            if (it != client_windows.end()) {
                                ++it;
                                if (it == client_windows.end()) {
                                    it = client_windows.begin();
                                }
                                focus_client(connection, *it);
                            } else {
                                focus_client(connection, client_windows[0]);
                            }
                        }
                    }
                    else if ((kp->detail == KEYCODE_K) && (current_modmask & modmask_super)) {
                        if (!client_windows.empty()) {
                            auto it = std::find(client_windows.begin(), client_windows.end(), focused_client_window);
                            if (it != client_windows.end()) {
                                if (it == client_windows.begin()) {
                                    it = client_windows.end() - 1;
                                } else {
                                    --it;
                                }
                                focus_client(connection, *it);
                            } else {
                                focus_client(connection, client_windows.back());
                            }
                        }
                    }

                    else if ((kp->detail == KEYCODE_PLUS) && (current_modmask & modmask_super)) {
                        gap_size += 2;
                        apply_master_stack(connection, screen);
                    }
                    else if ((kp->detail == KEYCODE_MINUS) && (current_modmask & modmask_super)) {
                        gap_size -= 2;
                        apply_master_stack(connection, screen);
                    }
                    break;
                }

                case XCB_FOCUS_IN: {
                    xcb_focus_in_event_t* fi = reinterpret_cast<xcb_focus_in_event_t *>(event);

                    bool is_client = false;
                    for (xcb_window_t client : client_windows) {
                        if (client == fi->event) {
                            is_client = true;
                            break;
                        }
                    }

                    if (is_client && fi->event != focused_client_window) {
                        focused_client_window = fi->event;
                    }
                    break;
                }
                case XCB_BUTTON_PRESS: {
                    auto* bp = (xcb_button_press_event_t *)event;
                    bool is_client_window = false;
                    for (xcb_window_t client_win : client_windows) {
                        if (client_win == bp->event) {
                            is_client_window = true;
                            break;
                        }
                    }
                    if (is_client_window && bp->event != focused_client_window) {
                        focus_client(connection, bp->event);
                    }
                    xcb_allow_events(connection, XCB_ALLOW_SYNC_POINTER, bp->time);
                    xcb_flush(connection);
                    break;
                }

                default: {
                    std::cout << "Unknown event type: " << (event->response_type & ~0x80) << std::endl;
                    break;
                }
            }
            free(event);
            event = nullptr;
        }
        end_loop:;
    }
    xcb_disconnect(connection);
    return 0;
}
