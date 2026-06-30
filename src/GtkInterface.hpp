/*
 * GtkInterface.hpp
 *
 *  Created on: 30 jun 2026
 *      Author: usuario001
 */

#ifndef GTKINTERFACE_HPP_
#define GTKINTERFACE_HPP_

#include <string>
#include <gtk/gtk.h>

// Inicializa y muestra la ventana principal
void build_interface(GtkApplication* app);
void setClaveMaestra(const std::string& clave);

#endif /* GTKINTERFACE_HPP_ */
