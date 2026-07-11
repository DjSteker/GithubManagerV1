//============================================================================
// Name        : GithubManager_V1.cpp
// Author      :
// Version     :
// Copyright   : Your copyright notice
// Description : Hello World in C++, Ansi-style
//$(shell pkg-config --cflags gtk4 tinyxml2 openssl)
//$(shell pkg-config --libs gtk4 tinyxml2 openssl)
//============================================================================

//#include <gtk/gtk.h>
//#include <tinyxml2.h>
//#include <openssl/sha.h>
//#include <filesystem>
//#include <string>
//#include <thread>
//#include <atomic>
//
//namespace fs = std::filesystem;
//
//// Variables globales para la UI
//static GtkWidget *directory_entry;
//static GtkWidget *repo_url_entry;
//static GtkWidget *branch_entry;
//static GtkWidget *commit_msg_entry;
//static GtkWidget *progress_bar;
//static GtkWidget *status_label;
//static GtkTextBuffer *log_buffer;
//
//void append_log(const char* message) {
//    if (!log_buffer) return;
//    GtkTextIter iter;
//    gtk_text_buffer_get_end_iter(log_buffer, &iter);
//    gtk_text_buffer_insert(log_buffer, &iter, message, -1);
//}
//
//std::string execute_command(const std::string& cmd, int* exit_code = nullptr) {
//    char buffer[256];
//    std::string result;
//    FILE* pipe = popen(cmd.c_str(), "r");
//    if (!pipe) {
//        if (exit_code) *exit_code = -1;
//        return "Error ejecutando comando";
//    }
//    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
//        result += buffer;
//    }
//    int status = pclose(pipe);
//    if (exit_code) *exit_code = WEXITSTATUS(status);
//    return result;
//}
//
//struct OpResult {
//    GtkWidget *widget;
//    GtkWidget *label;
//    GtkWidget *pb;
//};
//
//// Estructura para pasar el camino y el widget al callback de g_idle_add
//struct PathUpdateData {
//    gchar *path;
//    GtkWidget *entry_widget;
//};
//
//void run_git_operations(GtkWidget *trigger_widget) {
//    const gchar* dir_path = gtk_editable_get_text(GTK_EDITABLE(directory_entry));
//    const gchar* repo_url = gtk_editable_get_text(GTK_EDITABLE(repo_url_entry));
//    const gchar* branch = gtk_editable_get_text(GTK_EDITABLE(branch_entry));
//    const gchar* commit_msg = gtk_editable_get_text(GTK_EDITABLE(commit_msg_entry));
//
//    if (!dir_path || strlen(dir_path) == 0) {
//        g_idle_add([](gpointer data) -> gboolean {
//            gtk_label_set_text(GTK_LABEL(static_cast<GtkWidget*>(data)), "ERROR: Directorio vacío");
//            return G_SOURCE_REMOVE;
//        }, status_label);
//        return;
//    }
//
//    try {
//        g_idle_add([](gpointer data) -> gboolean {
//            gtk_label_set_text(GTK_LABEL(static_cast<GtkWidget*>(data)), "Iniciando proceso...");
//            gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress_bar));
//            return G_SOURCE_CONTINUE;
//        }, status_label);
//
//        fs::path git_dir = fs::path(dir_path) / ".git";
//        bool needs_clone = !fs::exists(git_dir);
//
//        if (needs_clone) {
//            append_log("Clonando repositorio...\n");
//            std::string clone_cmd = "git clone " + std::string(repo_url) + " " + std::string(dir_path);
//            int ec; execute_command(clone_cmd, &ec);
//            if(ec != 0) throw std::runtime_error("Falló el clone");
//        } else {
//            append_log("Usando repositorio existente...\n");
//        }
//
//        append_log("Añadiendo archivos...\n");
//        std::string add_cmd = "cd " + std::string(dir_path) + " && git add .";
//        execute_command(add_cmd);
//
//        append_log("Commit...\n");
//        std::string commit_cmd = "cd " + std::string(dir_path) + " && git commit -m \"" + std::string(commit_msg) + "\"";
//        execute_command(commit_cmd);
//
//        append_log("Push...\n");
//        std::string push_cmd = "cd " + std::string(dir_path) + " && git push -u origin " + std::string(branch);
//        execute_command(push_cmd);
//
//        append_log("¡Completado!\n");
//
//        g_idle_add([](gpointer user_data) -> gboolean {
//            auto r = static_cast<OpResult*>(user_data);
//            gtk_label_set_text(GTK_LABEL(r->label), "¡Subida completada!");
//            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(r->pb), 1.0);
//            gtk_widget_set_sensitive(r->widget, TRUE);
//            delete r;
//            return G_SOURCE_REMOVE;
//        }, new OpResult{trigger_widget, status_label, progress_bar});
//
//    } catch (const std::exception& e) {
//        // Guardar el mensaje de error en una cadena copiable antes de entrar al lambda
//        std::string err_msg = std::string("Error: ").append(e.what());
//
//        g_idle_add([](gpointer user_data) -> gboolean {
//            auto params = static_cast<std::pair<GtkWidget*, std::string>*>(user_data);
//            gtk_label_set_text(GTK_LABEL(params->first), params->second.c_str());
//            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0);
//            gtk_widget_set_sensitive(params->first, TRUE); // Habilitar botón padre si aplica
//            delete params;
//            return G_SOURCE_REMOVE;
//        }, new std::pair<GtkWidget*, std::string>{status_label, err_msg});
//    }
//}
//
//void start_upload(GtkWidget *widget, gpointer data) {
//    gtk_widget_set_sensitive(widget, FALSE);
//    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0);
//
//    std::thread t([=]() {
//        run_git_operations(widget);
//    });
//    t.detach();
//}
//
//// Callback asíncrono corregido
//void select_folder_callback(GObject *source_object, GAsyncResult *res, gpointer user_data) {
//    GtkWidget *target_entry = GTK_WIDGET(user_data);
//    GError *err = NULL;
//    GFile *file = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source_object), res, &err);
//
//    if (file) {
//        gchar *path = g_file_get_path(file);
//        if (path) {
//            // Crear estructura para pasar ambos datos
//            PathUpdateData *update_data = new PathUpdateData();
//            update_data->path = path;
//            update_data->entry_widget = target_entry;
//
//            g_idle_add([](gpointer p) -> gboolean {
//                PathUpdateData *data = static_cast<PathUpdateData*>(p);
//                if (data->path && data->entry_widget) {
//                    gtk_editable_set_text(GTK_EDITABLE(data->entry_widget), data->path);
//                }
//                g_free(data->path);
//                delete data;
//                return G_SOURCE_REMOVE;
//            }, update_data);
//
//            // No liberamos 'path' aquí porque se transfirió a update_data
//        }
//        g_object_unref(file);
//    }
//    if(err) g_clear_error(&err);
//}
//
//void on_select_clicked(GtkButton *button, gpointer data) {
//    GtkWidget *widget_to_find = GTK_WIDGET(button);
//    GtkWidget *win = NULL;
//
//    while (widget_to_find && !GTK_IS_WINDOW(widget_to_find)) {
//        widget_to_find = gtk_widget_get_parent(widget_to_find);
//        win = widget_to_find;
//    }
//
//    if (!win) win = GTK_WIDGET(data);
//
//    GtkFileDialog *dialog = gtk_file_dialog_new();
//
//    gtk_file_dialog_select_folder(dialog,
//                                  GTK_WINDOW(win),
//                                  NULL,
//                                  select_folder_callback,
//                                  data); // Pasamos el entry directamente
//}
//
//bool load_config(const std::string& config_path) {
//    tinyxml2::XMLDocument doc;
//    if (doc.LoadFile(config_path.c_str()) != tinyxml2::XML_SUCCESS) return false;
//    auto* element = doc.FirstChildElement("config");
//    if (!element) return false;
//    auto* repo_elem = element->FirstChildElement("repository");
//    if (repo_elem) {
//        const char* url = repo_elem->Attribute("url");
//        if (url) gtk_editable_set_text(GTK_EDITABLE(repo_url_entry), url);
//    }
//    return true;
//}
//
//void show_help(GtkButton *button, gpointer data) {
//    GtkWidget *win = gtk_window_new();
//    gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(data));
//    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
//    gtk_window_set_title(GTK_WINDOW(win), "Ayuda");
//    gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);
//
//    GtkWidget *txt = gtk_text_view_new();
//    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(txt), GTK_WRAP_WORD);
//    gtk_text_view_set_editable(GTK_TEXT_VIEW(txt), FALSE);
//
//    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(txt));
//    gtk_text_buffer_set_text(buf,
//        "Instrucciones:\n"
//        "1. Seleccione su carpeta local.\n"
//        "2. Introduzca URL del repositorio remoto.\n"
//        "3. Escriba mensaje de commit.\n"
//        "4. Pulse 'Subir'.\n\n"
//        "Nota: Git debe estar instalado y configurado.", -1);
//
//    GtkWidget *grid = gtk_grid_new();
//    gtk_grid_attach(GTK_GRID(grid), txt, 0, 0, 1, 1);
//    gtk_widget_set_vexpand(txt, TRUE);
//
//    GtkWidget *btn_close = gtk_button_new_with_label("Cerrar");
//    g_signal_connect_swapped(btn_close, "clicked", G_CALLBACK(gtk_window_close), win);
//    gtk_grid_attach(GTK_GRID(grid), btn_close, 0, 1, 1, 1);
//
//    gtk_window_set_child(GTK_WINDOW(win), grid);
//    gtk_window_present(GTK_WINDOW(win));
//}
//
//static void activate(GtkApplication *app, gpointer user_data) {
//    GtkWidget *window = gtk_application_window_new(app);
//    gtk_window_set_title(GTK_WINDOW(window), "GitHub Uploader GTK4");
//    gtk_window_set_default_size(GTK_WINDOW(window), 600, 500);
//
//    GtkWidget *grid = gtk_grid_new();
//    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
//    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
//    gtk_widget_set_margin_start(grid, 20);
//    gtk_widget_set_margin_end(grid, 20);
//    gtk_widget_set_margin_top(grid, 20);
//    gtk_widget_set_margin_bottom(grid, 20);
//
//    int row = 0;
//
//    // Directorio
//    GtkWidget *lbl_dir = gtk_label_new("<b>Directorio:</b>");
//    gtk_label_set_use_markup(GTK_LABEL(lbl_dir), TRUE);
//    gtk_grid_attach(GTK_GRID(grid), lbl_dir, 0, row++, 1, 1);
//
//    directory_entry = gtk_entry_new();
//    gtk_entry_set_placeholder_text(GTK_ENTRY(directory_entry), "/ruta/al/proyecto");
//
//    GtkWidget *btn_sel = gtk_button_new_with_label("📂");
//    gtk_widget_set_halign(btn_sel, GTK_ALIGN_END);
//    g_signal_connect(btn_sel, "clicked", G_CALLBACK(on_select_clicked), directory_entry);
//
//    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
//    gtk_box_append(GTK_BOX(hbox), directory_entry);
//    gtk_box_append(GTK_BOX(hbox), btn_sel);
//    gtk_grid_attach(GTK_GRID(grid), hbox, 1, row++, 1, 1);
//
//    // Repo URL
//    GtkWidget *lbl_url = gtk_label_new("<b>URL Repositorio:</b>");
//    gtk_label_set_use_markup(GTK_LABEL(lbl_url), TRUE);
//    gtk_grid_attach(GTK_GRID(grid), lbl_url, 0, row++, 1, 1);
//
//    repo_url_entry = gtk_entry_new();
//    gtk_entry_set_placeholder_text(GTK_ENTRY(repo_url_entry), "https://github.com/user/repo.git");
//    gtk_grid_attach(GTK_GRID(grid), repo_url_entry, 1, row++, 1, 1);
//
//    // Rama
//    GtkWidget *lbl_branch = gtk_label_new("<b>Rama:</b>");
//    gtk_label_set_use_markup(GTK_LABEL(lbl_branch), TRUE);
//    gtk_grid_attach(GTK_GRID(grid), lbl_branch, 0, row++, 1, 1);
//
//    branch_entry = gtk_entry_new();
//    gtk_editable_set_text(GTK_EDITABLE(branch_entry), "main");
//    gtk_grid_attach(GTK_GRID(grid), branch_entry, 1, row++, 1, 1);
//
//    // Mensaje
//    GtkWidget *lbl_msg = gtk_label_new("<b>Mensaje Commit:</b>");
//    gtk_label_set_use_markup(GTK_LABEL(lbl_msg), TRUE);
//    gtk_grid_attach(GTK_GRID(grid), lbl_msg, 0, row++, 1, 1);
//
//    commit_msg_entry = gtk_entry_new();
//    gtk_entry_set_placeholder_text(GTK_ENTRY(commit_msg_entry), "Actualización inicial");
//    gtk_grid_attach(GTK_GRID(grid), commit_msg_entry, 1, row++, 1, 1);
//
//    // Progress
//    progress_bar = gtk_progress_bar_new();
//    gtk_grid_attach(GTK_GRID(grid), progress_bar, 0, row++, 2, 1);
//
//    status_label = gtk_label_new("");
//    gtk_grid_attach(GTK_GRID(grid), status_label, 0, row++, 2, 1);
//
//    // Botones
//    GtkWidget *btn_upload = gtk_button_new_with_label("⬆ Subir a GitHub");
//    GtkWidget *btn_help = gtk_button_new_with_label("? Ayuda");
//    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
//    gtk_box_set_homogeneous(GTK_BOX(btn_box), TRUE);
//    gtk_box_append(GTK_BOX(btn_box), btn_help);
//    gtk_box_append(GTK_BOX(btn_box), btn_upload);
//
//    g_signal_connect(btn_upload, "clicked", G_CALLBACK(start_upload), NULL);
//    g_signal_connect(btn_help, "clicked", G_CALLBACK(show_help), window);
//    gtk_grid_attach(GTK_GRID(grid), btn_box, 0, row++, 2, 1);
//
//    // Log Area
//    GtkWidget *fr_log = gtk_frame_new("Logs");
//    GtkWidget *scrolled = gtk_scrolled_window_new();
//    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
//    gtk_widget_set_vexpand(scrolled, TRUE);
//
//    GtkWidget *log_view = gtk_text_view_new();
//    log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
//    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
//    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(log_view), GTK_WRAP_WORD_CHAR);
//    gtk_widget_set_size_request(log_view, -1, 100);
//
//    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), log_view);
//    gtk_frame_set_child(GTK_FRAME(fr_log), scrolled);
//    gtk_widget_set_vexpand(fr_log, TRUE);
//    gtk_grid_attach(GTK_GRID(grid), fr_log, 0, row, 2, 1);
//
//    gtk_window_set_child(GTK_WINDOW(window), grid);
//    gtk_window_present(GTK_WINDOW(window));
//}
//
//int main(int argc, char **argv) {
//    GtkApplication *app = gtk_application_new("org.example.github-uploader", G_APPLICATION_DEFAULT_FLAGS);
//    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
//    int status = g_application_run(G_APPLICATION(app), argc, argv);
//    g_object_unref(app);
//    return status;
//}
//
