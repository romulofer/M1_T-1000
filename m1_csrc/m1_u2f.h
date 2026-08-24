/* See COPYING.txt for license details. */

/*
*
* m1_u2f.h
*
* U2F/CTAP1 authenticator: menu entry, CTAPHID transport over USB, and
* encrypted on-SD storage of the per-device master secret.
*
* M1 Project
*
*/

#ifndef M1_U2F_H_
#define M1_U2F_H_

/* Menu entry point: switches USB to CTAPHID mode, serves REGISTER/
 * AUTHENTICATE/VERSION requests until BACK is pressed, then restores
 * CDC+MSC. Same blocking full-screen shape as gpio_usb_uart_bridge()
 * and badusb_main_menu(). */
void u2f_main_menu(void);

#endif /* M1_U2F_H_ */
