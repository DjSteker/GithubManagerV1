/*
 * GtkInterface.hpp
 */

#ifndef GTKINTERFACE_HPP_
#define GTKINTERFACE_HPP_

#include <gtk/gtk.h>
#include <string>


class Intefaz {

public:
  // La señal "activate" de GtkApplication pasa (GtkApplication*, gpointer).
  // La firma debe coincidir exactamente o el linker no encontrará el símbolo.
  static void build_interface(GtkApplication *app, gpointer user_data);

  // Establece la clave maestra desde código externo (p. ej. desde main.cpp
  // si se pasa por argumento de línea de comandos).
  void setClaveMaestra(const std::string &clave);

  static gboolean idle_restaurar_exito(gpointer data);
  static gboolean idle_restaurar_error(gpointer data);
  static gboolean idle_error_con_mensaje(gpointer data);
  static gboolean idle_actualizar_config_ui(gpointer data);
  static void on_select_folder_finish(GObject *src, GAsyncResult *res, gpointer user_data);
  static void on_select_folder(GtkButton *btn, gpointer data);
  static void on_clave_activate(GtkEntry *entry, gpointer user_data);
  static void run_git_upload(GtkWidget *boton, gpointer user_data);
  static void run_git_download(GtkWidget *boton, gpointer user_data);
  static void run_git_sync(GtkWidget *boton, gpointer user_data);

private:

  static void append_log(GtkTextBuffer *buffer, const char *mensaje);
  static gboolean idle_append_log(gpointer data);
  static void append_log_async(const std::string &mensaje);
  static void set_buttons_sensitive(gboolean sensitive);
  static void borrarSecreto(std::string &s);
  static void update_progress_with_message(const gchar *msg, gdouble fraction);
  static void run_git_compiler(GtkWidget *boton, gpointer user_data);
  static void cargar_configuracion_desde_xml(const std::string &directorio_path);

  // Datos para idle_actualizar_config_ui
  struct DatosConfigUI {
    std::string url;
    std::string rama;
    std::string token;
  };

  // Datos para idle_error_con_mensaje
  struct DatosMensaje {
    std::string mensaje;
  };

};

#endif /* GTKINTERFACE_HPP_ */
