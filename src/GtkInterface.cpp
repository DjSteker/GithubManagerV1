/*
 * GtkInterface.cpp
 *  Created on: 30 jun 2026
 *  Author: DjSteker
 */

#include "GtkInterface.hpp"
#include "GestorGit.hpp"
#include "Cifrado.hpp"
#include "RepoConfig.hpp"

#include <gtk/gtk.h>
#include <thread>
#include <string>
#include <filesystem>
#include <cstring>
#include <functional>
#include <openssl/crypto.h>

namespace fs = std::filesystem;

// ===========================================================================
// Widgets globales (single point of access)
// ===========================================================================
static GtkWidget *entry_clave = nullptr;
static GtkWidget *entry_directorio = nullptr;
static GtkWidget *entry_url = nullptr;
static GtkWidget *entry_rama = nullptr;
static GtkWidget *entry_mensaje = nullptr;
static GtkWidget *entry_token = nullptr;
static GtkWidget *check_guardar = nullptr;
static GtkWidget *label_estado = nullptr;
static GtkWidget *progress_bar = nullptr;
static GtkTextBuffer *buffer_log = nullptr;
static GtkWidget *text_view_log = nullptr;

static GtkWidget *btn_subir = nullptr;
static GtkWidget *btn_descargar = nullptr;
static GtkWidget *btn_sincronizar = nullptr;

// ===========================================================================
// Estructuras auxiliares para comunicación asíncrona
// ===========================================================================
struct LogData {
  GtkWidget *label;
  gchar *texto;
};

struct IdleLogData {
  GtkTextBuffer *buf;
  char *msg;
};

// ===========================================================================
// Funciones de Logging (hilo principal y seguro para hilos secundarios)
// ===========================================================================
void append_log(GtkTextBuffer *buffer, const char *mensaje) {
  if (!buffer || !mensaje) return;

  GtkTextIter iter;
  gtk_text_buffer_get_end_iter(buffer, &iter);
  gtk_text_buffer_insert(buffer, &iter, mensaje, -1);

  if (text_view_log) {
    gtk_text_buffer_get_end_iter(buffer, &iter);
    GtkTextMark *mark = gtk_text_buffer_create_mark(buffer, NULL, &iter, FALSE);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(text_view_log), mark, 0.0, FALSE, 0, 0);
    gtk_text_buffer_delete_mark(buffer, mark);
  }
}

gboolean idle_append_log(gpointer data) {
  auto *d = static_cast<IdleLogData *>(data);
  if (d && d->buf && d->msg) {
    append_log(d->buf, d->msg);
  }
  if (d) {
    if (d->msg) g_free(d->msg);
    delete d;
  }
  return G_SOURCE_REMOVE;
}

void append_log_async(const std::string &mensaje) {
  IdleLogData *d = new IdleLogData();
  d->buf = buffer_log;
  d->msg = g_strdup(mensaje.c_str());
  g_idle_add(idle_append_log, d);
}

// ===========================================================================
// Helpers UI (botones y estado)
// ===========================================================================
static void set_buttons_sensitive(gboolean sensitive) {
  if (btn_subir) gtk_widget_set_sensitive(btn_subir, sensitive);
  if (btn_descargar) gtk_widget_set_sensitive(btn_descargar, sensitive);
  if (btn_sincronizar) gtk_widget_set_sensitive(btn_sincronizar, sensitive);
}

// Lambda universal para limpieza segura de secretos en memoria
auto borrar_secreto = [](std::string &s) {
  if (!s.empty()) {
    OPENSSL_cleanse(&s[0], s.size());
    s.clear();
    s.shrink_to_fit();
  }
};

// ===========================================================================
// FUNCIÓN UNIFICADA: Cargar Configuración desde XML
// ===========================================================================
/**
 * Carga configuración previamente guardada para un directorio específico.
 * Intenta desencriptar el token usando la clave maestra actual de la UI.
 *
 * @param directorio_path Ruta absoluta del directorio/repositorio local
 */
void cargar_configuracion_desde_xml(const std::string &directorio_path) {
  // Capturar clave actual de la UI (copia local segura)
  std::string clave_local = gtk_editable_get_text(GTK_EDITABLE(entry_clave));

  std::thread t_cargar([directorio_path, clave_local]() mutable {
    ConfigRepo cfg;
    std::string token_plain;
    std::string url_final;
    std::string rama_final;

    try {
      // Intentar cargar archivo XML asociado al directorio
      if (GestorConfig::cargar(directorio_path, cfg)) {
        url_final = cfg.url;
        rama_final = cfg.rama;

        append_log_async("✓ Archivo de configuración encontrado\n");

        // Intentar desencriptar el token con la clave maestra puesta
        if (!cfg.tokenEncriptado.empty() && !clave_local.empty()) {
          try {
            token_plain = Cifrado::desencriptar(cfg.tokenEncriptado, clave_local);

            if (!token_plain.empty()) {
              append_log_async("  → Token desencriptado exitosamente ✓\n");
            } else {
              append_log_async("  ⚠ Token desencriptado vacío (¿clave incorrecta?)\n");
            }
          } catch (const std::exception &e) {
            append_log_async("  ⚠ Error desencriptando token: " + std::string(e.what()) + "\n");
            append_log_async("     Asegúrate de usar la misma clave que usaste al guardar\n");
          }
        } else if (!cfg.tokenEncriptado.empty() && clave_local.empty()) {
          append_log_async("  ℹ Hay token cifrado pero falta clave maestra\n");
          append_log_async("     Introduce la clave y presiona [Enter]\n");
        } else {
          append_log_async("  ℹ Configuración sin token cifrado\n");
        }

        // Actualizar UI en hilo principal (Thread-Safe)
        g_idle_add([](gpointer d) -> gboolean {
          auto *func = static_cast<std::function<void()> *>(d);
          (*func)();
          delete func;
          return G_SOURCE_REMOVE;
        },
                   new std::function<void()>([url_final, rama_final, token_plain]() mutable {
                     gtk_editable_set_text(GTK_EDITABLE(entry_url), url_final.c_str());
                     gtk_editable_set_text(GTK_EDITABLE(entry_rama), rama_final.c_str());

                     if (!token_plain.empty()) {
                       gtk_editable_set_text(GTK_EDITABLE(entry_token), token_plain.c_str());
                       append_log(buffer_log, "✓ Configuración cargada completa\n");

                       // RECURSO CRÍTICO: Notificar sobre PAT obligatorio
                       append_log(buffer_log, "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
                       append_log(buffer_log, "🔐 IMPORTANTE: GitHub requiere Personal Access Token (PAT)\n");
                       append_log(buffer_log, "   · NO uses tu contraseña normal de GitHub\n");
                       append_log(buffer_log, "   · Genera un PAT en: https://github.com/settings/tokens\n");
                       append_log(buffer_log, "   · Scope recomendado: repo (full repository access)\n");
                       append_log(buffer_log, "   · Para forks: public_repo basta si es público\n");
                       append_log(buffer_log, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
                     }

                     // Limpiar después de enviar a UI
                     borrar_secreto(token_plain);
                     borrar_secreto(url_final);
                     borrar_secreto(rama_final);
                   }));

      } else {
        append_log_async("ℹ No hay configuración guardada para este repositorio\n");
        append_log_async("💡 Tip: Activa 'Guardar credenciales' antes del primer SUBIR\n\n");
      }

    } catch (const std::exception &e) {
      std::string error_msg = "⚠ Error cargando configuración: " + std::string(e.what()) + "\n";
      g_idle_add([](gpointer d) -> gboolean {
        auto *msg = static_cast<std::string *>(d);
        append_log(buffer_log, msg->c_str());
        delete msg;
        return G_SOURCE_REMOVE;
      },
                 new std::string(error_msg));

      borrar_secreto(token_plain);
      borrar_secreto(url_final);
      borrar_secreto(rama_final);
    } catch (...) {
      append_log_async("⚠ Error desconocido al cargar configuración\n");
      borrar_secreto(token_plain);
      borrar_secreto(url_final);
      borrar_secreto(rama_final);
    }

    // Siempre limpiar clave capturada
    borrar_secreto(clave_local);
  });

  t_cargar.detach();
}

// ===========================================================================
// Selector de Carpeta (con carga automática de XML)
// ===========================================================================
void on_select_folder(GtkButton *btn, gpointer data) {
  GtkWindow *win = GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(btn)));
  if (!win) win = GTK_WINDOW(data);

  GtkFileDialog *dialog = gtk_file_dialog_new();

  gtk_file_dialog_select_folder(
    dialog, win, NULL,
    [](GObject *src, GAsyncResult *res, gpointer udata) {
      GFile *file = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(src), res, NULL);
      if (!file) return;

      gchar *path_raw = g_file_get_path(file);
      std::string path_str(path_raw ? path_raw : "");
      g_free(path_raw);
      g_object_unref(file);

      // Mostrar ruta inmediatamente
      if (!path_str.empty()) {
        gtk_editable_set_text(GTK_EDITABLE(entry_directorio), path_str.c_str());

        // Trigger automático de carga de configuración
        append_log_async("\n📂 Directorio seleccionado: " + path_str + "\n");
        append_log_async("   Buscando configuración previa...\n");

        cargar_configuracion_desde_xml(path_str);
      }
    },
    NULL);
}

// ===========================================================================
// Puente: Enter en campo Clave Maestra → recarga configuración
// ===========================================================================
static void on_clave_activate(GtkEntry *entry, gpointer user_data) {
  // btn_sel ya no se usa aquí, simplemente ignoramos el parámetro
  const char *dir_actual = gtk_editable_get_text(GTK_EDITABLE(entry_directorio));

  if (dir_actual && strlen(dir_actual) > 0) {
    std::string directorio_path(dir_actual);

    append_log(buffer_log, "\n🔄 Presionaste [Enter] en Clave Maestra\n");
    append_log(buffer_log, "   Recargando configuración con nueva clave...\n");

    cargar_configuracion_desde_xml(directorio_path);

  } else {
    append_log(buffer_log, "\nℹ Primero selecciona o introduce un directorio\n");
  }
}

// ===========================================================================
// RUN GIT UPLOAD (SUBIR)
// ===========================================================================
void run_git_upload(GtkWidget *boton, gpointer user_data) {
  set_buttons_sensitive(FALSE);
  if (progress_bar) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0);
  append_log(buffer_log, "\n━═━═━═━═━═━═━═━═━═━═━═━═━═━═━═━═━\n");
  append_log(buffer_log, "--- INICIANDO OPERACIÓN SUBIR ---\n");

  std::string clave = gtk_editable_get_text(GTK_EDITABLE(entry_clave));
  std::string dir_path = gtk_editable_get_text(GTK_EDITABLE(entry_directorio));
  std::string url_repo = gtk_editable_get_text(GTK_EDITABLE(entry_url));
  std::string rama = gtk_editable_get_text(GTK_EDITABLE(entry_rama));
  std::string mensaje = gtk_editable_get_text(GTK_EDITABLE(entry_mensaje));
  std::string token = gtk_editable_get_text(GTK_EDITABLE(entry_token));
  bool guardar = gtk_check_button_get_active(GTK_CHECK_BUTTON(check_guardar));

  std::thread t([=]() mutable {
    ResultadoOperacionGit resultado;

    try {
      // Validar dirección crítica: DIR vacía
      if (dir_path.empty()) {
        std::string err = "❌ ERROR: Directorio local vacío\nIntroduce una ruta o selecciónala con 📁\n";
        g_idle_add([](gpointer d) -> gboolean {
          append_log(buffer_log, reinterpret_cast<const char *>(d));
          set_buttons_sensitive(TRUE);
          gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0);
          delete reinterpret_cast<std::string *>(d);
          return G_SOURCE_REMOVE;
        },
                   new std::string(err));
        return;
      }

      // Guardar configuración cifrada si marcado
      if (guardar && !clave.empty() && !dir_path.empty()) {
        ConfigRepo cfg;
        cfg.url = url_repo;
        cfg.directorio = dir_path;
        cfg.rama = rama.empty() ? "main" : rama;

        try {
          cfg.tokenEncriptado = Cifrado::encriptar(token, clave);
          if (GestorConfig::guardar(cfg)) {
            append_log_async("✓ Configuración GUARDADA cifrada\n");
            append_log_async("  Ubicación: " + GestorConfig::archivoPara(dir_path) + "\n");
          } else {
            append_log_async("⚠ No se pudo guardar configuración\n");
          }
        } catch (const std::exception &e) {
          append_log_async("✗ Error cifrando token: " + std::string(e.what()) + "\n");
        }
      }

      // Validar URL remota
      if (!url_repo.empty() && !GestorGit::validarUrlRepositorio(url_repo)) {
        std::string err = "❌ ERROR: URL inválida\nNo usar credenciales embebidas: usuario@host\nFormato correcto: https://github.com/user/repo.git\n";
        append_log_async(err);

        g_idle_add([](gpointer d) -> gboolean {
          set_buttons_sensitive(TRUE);
          gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0);
          return G_SOURCE_REMOVE;
        },
                   nullptr);
        return;
      }

      // Alerta crítica sobre TOKEN vs CONTRASEÑA
      if (!token.empty()) {
        if (token.length() < 20 || token.find_first_of('/') != std::string::npos) {
          append_log_async("\n⚠ ALERTA: El token parece demasiado corto o inválido\n");
          append_log_async("   ¿Estás usando una contraseña en vez de PAT?\n");
          append_log_async("   Patrones válidos: ghp_xxxx, ghu_xxxx, ghy_xxxx (~40 caracteres)\n\n");
        }
      }

      // Ejecutar operaciones Git
      bool esRepoNuevo = !fs::exists(fs::path(dir_path) / ".git");

      if (esRepoNuevo) {
        append_log_async("📦 Repositorio NUEVO detectado\n");

        if (!url_repo.empty()) {
          if (fs::exists(dir_path)) {
            bool vacio = (fs::directory_iterator(dir_path) == fs::directory_iterator());
            if (!vacio) {
              append_log_async("   → Directorio existente no vacío\n   Inicializando Git...");
              resultado = GestorGit::subirCambios(dir_path, rama, mensaje, token, url_repo);
            } else {
              append_log_async("   → Directorio vacío\n   Clonando repositorio...");
              resultado = GestorGit::clonarRepositorio(url_repo, dir_path, token);
              if (resultado.exito) {
                append_log_async("\n   ✅ Clonación completada\n   Subiendo cambios iniciales...");
                resultado = GestorGit::subirCambios(dir_path, rama, mensaje, token, "");
              }
            }
          } else {
            append_log_async("   → Creando directorio y clonando...");
            resultado = GestorGit::clonarRepositorio(url_repo, dir_path, token);
            if (resultado.exito) {
              append_log_async("\n   ✅ Clonación completada\n   Subiendo cambios iniciales...");
              resultado = GestorGit::subirCambios(dir_path, rama, mensaje, token, "");
            }
          }
        } else {
          append_log_async("   Sin URL remota → Solo inicialización local");
          resultado = GestorGit::subirCambios(dir_path, rama, mensaje, token, "");
        }
      } else {
        append_log_async("🔄 Repositorio EXISTENTE\n   Subiendo cambios locales...");
        resultado = GestorGit::subirCambios(dir_path, rama, mensaje, token, "");
      }

      // Reportar resultado
      std::string estadoStr = resultado.exito ? "✅ ÉXITO" : "❌ FALLIDO";
      std::string msg_final = "\n──────────────────────────────────────────────\n";
      msg_final += "Resultado FINAL: " + estadoStr + "\n";
      msg_final += "Mensaje: " + resultado.mensaje + "\n\n";
      if (!resultado.salidaCompleta.empty()) {
        msg_final += "Salida Git:\n" + resultado.salidaCompleta;
      }
      msg_final += "──────────────────────────────────────────────\n\n";

      append_log_async(msg_final);

      // Reactivar UI
      g_idle_add([](gpointer d) -> gboolean {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 1.0);
        set_buttons_sensitive(TRUE);
        return G_SOURCE_REMOVE;
      },
                 nullptr);

    } catch (const std::exception &e) {
      std::string err = "💥 Excepción en operación: " + std::string(e.what()) + "\n";
      append_log_async(err);

      g_idle_add([](gpointer d) -> gboolean {
        set_buttons_sensitive(TRUE);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0);
        return G_SOURCE_REMOVE;
      },
                 nullptr);
    }

    // 🔒 LIMPIEZA TOTAL Y SEGURA DE MEMORIA
    borrar_secreto(clave);
    borrar_secreto(token);
    borrar_secreto(mensaje);
    borrar_secreto(url_repo);
    borrar_secreto(dir_path);
    borrar_secreto(rama);
  });

  t.detach();
}

// ===========================================================================
// RUN GIT DOWNLOAD (DESCARGAR/PULL)
// ===========================================================================
void run_git_download(GtkWidget *boton, gpointer user_data) {
  set_buttons_sensitive(FALSE);
  if (progress_bar) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0);
  append_log(buffer_log, "\n━═━═━═━═━═━═━═━═━═━═━═━═━═━═━═━═━\n");
  append_log(buffer_log, "--- INICIANDO OPERACIÓN DESCARGAR ---\n");

  std::string dir_path = gtk_editable_get_text(GTK_EDITABLE(entry_directorio));
  std::string rama = gtk_editable_get_text(GTK_EDITABLE(entry_rama));
  std::string token = gtk_editable_get_text(GTK_EDITABLE(entry_token));

  std::thread t([=]() mutable {
    try {
      if (dir_path.empty()) {
        append_log_async("❌ ERROR: Directorio local vacío\n");
      } else if (!fs::exists(fs::path(dir_path) / ".git")) {
        append_log_async("❌ ERROR: No es un repositorio Git (.git faltante)\n");
      } else {
        append_log_async("📥 Obteniendo cambios remotos (pull)...\n");
        ResultadoOperacionGit resultado = GestorGit::bajarCambios(dir_path, rama, token);

        std::string estadoStr = resultado.exito ? "✅ ÉXITO" : "❌ FALLIDO";
        std::string msg_final = "\n──────────────────────────────────────────────\n";
        msg_final += "Resultado: " + estadoStr + "\n";
        msg_final += "Mensaje: " + resultado.mensaje + "\n\n";
        msg_final += "Salida:\n" + resultado.salidaCompleta;
        msg_final += "──────────────────────────────────────────────\n\n";

        append_log_async(msg_final);
      }

      g_idle_add([](gpointer d) -> gboolean {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 1.0);
        set_buttons_sensitive(TRUE);
        return G_SOURCE_REMOVE;
      },
                 nullptr);

    } catch (const std::exception &e) {
      append_log_async("💥 Excepción: " + std::string(e.what()) + "\n");
      g_idle_add([](gpointer d) -> gboolean {
        set_buttons_sensitive(TRUE);
        return G_SOURCE_REMOVE;
      },
                 nullptr);
    }

    borrar_secreto(token);
    borrar_secreto(dir_path);
    borrar_secreto(rama);
  });

  t.detach();
}

// ===========================================================================
// RUN GIT SYNC (SINCRONIZAR - Pull + Push)
// ===========================================================================
void run_git_sync(GtkWidget *boton, gpointer user_data) {
  set_buttons_sensitive(FALSE);
  if (progress_bar) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0);
  append_log(buffer_log, "\n━═━═━═━═━═━═━═━═━═━═━═━═━═━═━═━═━\n");
  append_log(buffer_log, "--- INICIANDO SINCRONIZACIÓN ---\n");

  std::string clave = gtk_editable_get_text(GTK_EDITABLE(entry_clave));
  std::string dir_path = gtk_editable_get_text(GTK_EDITABLE(entry_directorio));
  std::string rama = gtk_editable_get_text(GTK_EDITABLE(entry_rama));
  std::string mensaje = gtk_editable_get_text(GTK_EDITABLE(entry_mensaje));
  std::string token = gtk_editable_get_text(GTK_EDITABLE(entry_token));

  std::thread t([=]() mutable {
    try {
      if (dir_path.empty()) {
        append_log_async("❌ ERROR: Directorio vacío\n");
      } else if (!fs::exists(fs::path(dir_path) / ".git")) {
        append_log_async("❌ ERROR: No es repositorio Git\n");
      } else {
        // PASO 1: Pull
        append_log_async("📥 Paso 1/2: Descargando remotos (pull)...\n");
        ResultadoOperacionGit pullResult = GestorGit::bajarCambios(dir_path, rama, token);

        if (!pullResult.exito) {
          append_log_async("⚠️ Pull falló: " + pullResult.mensaje + "\n");
          append_log_async("   Verifica conexión internet y credenciales\n\n");
        } else {
          append_log_async("✅ Pull completado\n\n");
        }

        // PASO 2: Push
        append_log_async("📤 Paso 2/2: Subiendo cambios locales (push)...\n");
        ResultadoOperacionGit pushResult = GestorGit::subirCambios(dir_path, rama, mensaje, token, "");

        if (!pushResult.exito) {
          append_log_async("⚠️ Push falló: " + pushResult.mensaje + "\n");
          append_log_async("   Posibles causas: conflictos de merge, token expirado, permissions\n\n");
        } else {
          append_log_async("✅ Push completado\n\n");
        }

        // RESULTADO FINAL - CORREGIDO PARA OPERADOR +
        bool ambosExito = pullResult.exito && pushResult.exito;
        std::string estadoStr = ambosExito ? std::string("✅ TODO OK") : std::string("⚠ PARCIAL");
        std::string pullIcon = pullResult.exito ? std::string("✅") : std::string("❌");
        std::string pushIcon = pushResult.exito ? std::string("✅") : std::string("❌");

        std::string msg_final = "\n═══════════════════════════════════════════\n";
        msg_final += "SINCRONIZACIÓN: " + estadoStr + "\n";
        msg_final += "Pull: " + pullIcon + " | ";   // ← CONCERTTADO A std::string
        msg_final += "Push: " + pushIcon + "\n\n";  // ← CONVERTIDO A std::string
        msg_final += "--- DETALLE PULL ---\n" + pullResult.salidaCompleta + "\n";
        msg_final += "--- DETALLE PUSH ---\n" + pushResult.salidaCompleta + "\n";
        msg_final += "═══════════════════════════════════════════\n\n";

        append_log_async(msg_final);
      }

      g_idle_add([](gpointer d) -> gboolean {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 1.0);
        set_buttons_sensitive(TRUE);
        return G_SOURCE_REMOVE;
      },
                 nullptr);

    } catch (const std::exception &e) {
      append_log_async("💥 Excepción: " + std::string(e.what()) + "\n");
      g_idle_add([](gpointer d) -> gboolean {
        set_buttons_sensitive(TRUE);
        return G_SOURCE_REMOVE;
      },
                 nullptr);
    }

    borrar_secreto(clave);
    borrar_secreto(token);
    borrar_secreto(mensaje);
    borrar_secreto(dir_path);
    borrar_secreto(rama);
  });

  t.detach();
}

// ===========================================================================
// BUILD INTERFACE
// ===========================================================================
// ===========================================================================
// BUILD INTERFACE (GTK4 corregido)
// ===========================================================================
void build_interface(GtkApplication *app) {
  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Gestor Git V1.0 🔐");
  gtk_window_set_default_size(GTK_WINDOW(window), 720, 750);

  // Grid principal
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 15);
  gtk_widget_set_margin_start(grid, 20);
  gtk_widget_set_margin_end(grid, 20);
  gtk_widget_set_margin_top(grid, 20);
  gtk_widget_set_margin_bottom(grid, 20);

  int row = 0;

  // ====================== CLAVE MAESTRA ======================
  GtkWidget *lbl_clave = gtk_label_new("<b>🔑 Clave Maestra:</b>");
  gtk_label_set_use_markup(GTK_LABEL(lbl_clave), TRUE);
  gtk_grid_attach(GTK_GRID(grid), lbl_clave, 0, row, 1, 1);

  entry_clave = gtk_password_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry_clave), "Usada para cifrar/descifrar tokens guardados");
  gtk_widget_set_tooltip_text(entry_clave, "Presiona [Enter] aquí para recargar configuración con esta clave");
  gtk_grid_attach(GTK_GRID(grid), entry_clave, 1, row++, 1, 1);

  // ====================== DIRECTORIO LOCAL ====================
  GtkWidget *lbl_dir = gtk_label_new("<b>📁 Directorio Local:</b>");
  gtk_label_set_use_markup(GTK_LABEL(lbl_dir), TRUE);
  gtk_grid_attach(GTK_GRID(grid), lbl_dir, 0, row, 1, 1);

  entry_directorio = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry_directorio), "/home/usuario/proyectos/mi-proyecto");

  GtkWidget *btn_sel = gtk_button_new_with_label("Seleccionar");
  gtk_widget_set_tooltip_text(btn_sel, "Selecciona carpeta del proyecto (carga XML automáticamente)");
  g_signal_connect(btn_sel, "clicked", G_CALLBACK(on_select_folder), NULL);

  GtkWidget *hbox_dir = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_box_append(GTK_BOX(hbox_dir), entry_directorio);
  gtk_box_append(GTK_BOX(hbox_dir), btn_sel);
  gtk_widget_set_hexpand(entry_directorio, TRUE);
  gtk_grid_attach(GTK_GRID(grid), hbox_dir, 1, row++, 1, 1);

  // ====================== URL REMOTO ==========================
  GtkWidget *lbl_url = gtk_label_new("<b>🌐 URL Remoto:</b>");
  gtk_label_set_use_markup(GTK_LABEL(lbl_url), TRUE);
  gtk_grid_attach(GTK_GRID(grid), lbl_url, 0, row, 1, 1);

  entry_url = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry_url), "https://github.com/DjSteker/GithubManagerV1.git");
  gtk_widget_set_tooltip_text(entry_url, "HTTPS limpio sin credenciales: https://github.com/user/repo.git");
  gtk_grid_attach(GTK_GRID(grid), entry_url, 1, row++, 1, 1);

  // ====================== RAMA =================================
  GtkWidget *lbl_ram = gtk_label_new("<b>🌿 Rama:</b>");
  gtk_label_set_use_markup(GTK_LABEL(lbl_ram), TRUE);
  gtk_grid_attach(GTK_GRID(grid), lbl_ram, 0, row, 1, 1);

  entry_rama = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(entry_rama), "main");
  gtk_grid_attach(GTK_GRID(grid), entry_rama, 1, row++, 1, 1);

  // ====================== MENSAJE COMMIT =======================
  GtkWidget *lbl_msg = gtk_label_new("<b>✏️ Mensaje Commit:</b>");
  gtk_label_set_use_markup(GTK_LABEL(lbl_msg), TRUE);
  gtk_grid_attach(GTK_GRID(grid), lbl_msg, 0, row, 1, 1);

  entry_mensaje = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry_mensaje), "Actualización automática del código");
  gtk_grid_attach(GTK_GRID(grid), entry_mensaje, 1, row++, 1, 1);

  // ====================== TOKEN PAT =============================
  GtkWidget *lbl_tok = gtk_label_new("<b>🔐 Token GitHub (PAT):</b>");
  gtk_label_set_use_markup(GTK_LABEL(lbl_tok), TRUE);
  gtk_grid_attach(GTK_GRID(grid), lbl_tok, 0, row, 1, 1);

  entry_token = gtk_password_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry_token), "ghp_xxxxxxxxxx (Personal Access Token)");
  gtk_widget_set_tooltip_text(entry_token, "IMPORTANTE: Usa PAT generado en github.com/settings/tokens\nNO uses contraseña normal de cuenta");
  gtk_grid_attach(GTK_GRID(grid), entry_token, 1, row++, 1, 1);

  // ====================== CHECKBOX GUARDAR =====================
  check_guardar = gtk_check_button_new_with_label("💾 Guardar credenciales cifradas (XML por repo)");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(check_guardar), TRUE);
  gtk_widget_set_tooltip_text(check_guardar, "Crea archivo XML único por directorio para carga automática futura");
  gtk_grid_attach(GTK_GRID(grid), check_guardar, 1, row++, 1, 1);

  // ====================== PROGRESO Y ESTADO ====================
  progress_bar = gtk_progress_bar_new();
  gtk_grid_attach(GTK_GRID(grid), progress_bar, 0, row++, 2, 1);

  label_estado = gtk_label_new("Estado: Listo para operar");
  gtk_grid_attach(GTK_GRID(grid), label_estado, 0, row++, 2, 1);

  // ====================== BOTONES ACCIÓN ========================
  GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_halign(button_box, GTK_ALIGN_FILL);
  gtk_widget_set_hexpand(button_box, TRUE);

  btn_descargar = gtk_button_new_with_label("⬇ DESCARGAR");
  g_signal_connect(btn_descargar, "clicked", G_CALLBACK(run_git_download), NULL);
  gtk_widget_set_size_request(btn_descargar, -1, 45);
  gtk_widget_set_tooltip_text(btn_descargar, "Git pull únicamente");
  gtk_box_append(GTK_BOX(button_box), btn_descargar);

  btn_sincronizar = gtk_button_new_with_label("🔄 SINCRONIZAR");
  g_signal_connect(btn_sincronizar, "clicked", G_CALLBACK(run_git_sync), NULL);
  gtk_widget_set_size_request(btn_sincronizar, -1, 45);
  gtk_widget_set_tooltip_text(btn_sincronizar, "Pull + Push (ambas direcciones)");
  gtk_box_append(GTK_BOX(button_box), btn_sincronizar);

  btn_subir = gtk_button_new_with_label("⬆ SUBIR");
  g_signal_connect(btn_subir, "clicked", G_CALLBACK(run_git_upload), NULL);
  gtk_widget_set_size_request(btn_subir, -1, 45);
  gtk_widget_set_tooltip_text(btn_subir, "Commit + Push (local a remoto)");
  gtk_box_append(GTK_BOX(button_box), btn_subir);

  gtk_grid_attach(GTK_GRID(grid), button_box, 0, row++, 2, 1);

  // ====================== REGISTRO LOG =========================
  GtkWidget *frame_log = gtk_frame_new("📋 Registro de Operaciones");
  GtkWidget *scrolled = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scrolled, TRUE);
  gtk_widget_set_size_request(scrolled, -1, 180);

  text_view_log = gtk_text_view_new();
  buffer_log = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view_log));
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view_log), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view_log), GTK_WRAP_WORD_CHAR);
  gtk_widget_set_vexpand(text_view_log, TRUE);

  // === CSS para fuente monospace (GTK4 compatible) ===
  GtkCssProvider *provider = gtk_css_provider_new();
  const gchar *css_data = R"(
        .monospace-log {
            font-family: monospace, 'Liberation Mono', 'Courier New', 'DejaVu Sans Mono';
            font-size: 10px;
        }
    )";

  gtk_css_provider_load_from_string(provider, css_data);

  gtk_style_context_add_provider_for_display(
    gdk_display_get_default(),
    GTK_STYLE_PROVIDER(provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  gtk_widget_add_css_class(text_view_log, "monospace-log");

  g_object_unref(provider);

  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), text_view_log);
  gtk_frame_set_child(GTK_FRAME(frame_log), scrolled);
  gtk_widget_set_vexpand(frame_log, TRUE);
  gtk_grid_attach(GTK_GRID(grid), frame_log, 0, row, 2, 1);

  // === CONEXIONES DE SEÑALES ===
  g_signal_connect(entry_clave, "activate", G_CALLBACK(on_clave_activate), NULL);

  // Welcome message
  append_log(buffer_log, "╔═══════════════════════════════════════════════════════╗\n");
  append_log(buffer_log, "║           GESTOR GIT V1.0 - Interfaz GTK4            ║\n");
  append_log(buffer_log, "║          Sistema Seguro de Gestión de Repos          ║\n");
  append_log(buffer_log, "╚═══════════════════════════════════════════════════════╝\n\n");
  append_log(buffer_log, "Bienvenido.\n");
  append_log(buffer_log, "· Selecciona un directorio para empezar\n");
  append_log(buffer_log, "· Configura tu Token PAT de GitHub\n");
  append_log(buffer_log, "· Marca 'Guardar credenciales' para carga automática\n\n");
  append_log(buffer_log, "🔗 Genera PAT en: https://github.com/settings/tokens\n");
  append_log(buffer_log, "Scope recomendado: repo (Full control private repos)\n\n");

  gtk_window_set_child(GTK_WINDOW(window), grid);
  gtk_window_present(GTK_WINDOW(window));
}
