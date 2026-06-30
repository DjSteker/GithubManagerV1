/*
 * GtkInterface.hpp
 */

#ifndef GTKINTERFACE_HPP_
#define GTKINTERFACE_HPP_

#include <gtk/gtk.h>
#include <string>

// La señal "activate" de GtkApplication pasa (GtkApplication*, gpointer).
// La firma debe coincidir exactamente o el linker no encontrará el símbolo.
void build_interface(GtkApplication *app, gpointer user_data);

// Establece la clave maestra desde código externo (p. ej. desde main.cpp
// si se pasa por argumento de línea de comandos).
void setClaveMaestra(const std::string &clave);

#endif /* GTKINTERFACE_HPP_ */
