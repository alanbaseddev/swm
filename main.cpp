#include <iostream>
#include <oneapi/tbb/profiling.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
int main() {
    xcb_connection_t* connection;
    xcb_screen_t* screen;
    connection = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(connection)) {
        std::cerr << "Failed to connect to X server" << std::endl;
        return 1;
    }
    std::cout << "Connected to X server" << std::endl;
    const xcb_setup_t* setup = xcb_get_setup(connection);
    screen = xcb_setup_roots_iterator(setup).data;
    if ( screen ) {
        std::cout << screen->width_in_pixels << "x" << screen->height_in_pixels << std::endl;
        uint32_t mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_STRUCTURE_NOTIFY;
        xcb_change_window_attributes(connection, screen->root, XCB_CW_EVENT_MASK, &mask);
        xcb_flush(connection);
        xcb_generic_event_t* event;
        while ((event = xcb_wait_for_event(connection))) {
            switch ( event -> response_type & ~0x80 ) {
                case XCB_MAP_REQUEST: {
                    xcb_map_request_event_t* mr = (xcb_map_request_event_t*)event;
                    xcb_map_window(connection, mr->window);
                    xcb_flush(connection);
                    break;
                }
                case XCB_CONFIGURE_REQUEST: {
                    xcb_configure_request_event_t* cr = (xcb_configure_request_event_t*)event;
                    std::cout << "ConfigureRequest received for window: " << cr->window << std::endl;
                    uint32_t values[7];
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
                    xcb_flush(connection);
                    break;
                }
                case XCB_DESTROY_NOTIFY: {
                    xcb_destroy_notify_event_t* dn = (xcb_destroy_notify_event_t*)event;
                    std::cout << "Window destroyed" << dn->window << std::endl;
                    break;
                }
                default: {
                    std::cout << "Unknown event type" << std::endl;
                    break;
                }
            }}
        free(event);
    }
    xcb_disconnect(connection);

    return 0;
}