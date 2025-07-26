#include <iostream>
#include <xcb/xcb.h>
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
    }
    xcb_disconnect(connection);

    return 0;
}