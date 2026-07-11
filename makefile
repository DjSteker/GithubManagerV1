##############################################################################
# GithubManager V1 - Makefile con Ofuscación de Contraseña en Compilación
##############################################################################

# --- Variables ---
CC       := g++
CFLAGS   := -std=c++17 -Wall -Wextra -O2 $(shell pkg-config --cflags gtk4 tinyxml2 openssl) -fPIC -fPIE
LDFLAGS  := $(shell pkg-config --libs gtk4 tinyxml2 openssl)
SRCDIR   := src
OBJDIR   := build
TARGET   := GithubManager

# Archivos fuente
SOURCES  := $(SRCDIR)/main.cpp \
            $(SRCDIR)/Cifrado.cpp \
            $(SRCDIR)/PasswordObfuscator.cpp \
            $(SRCDIR)/RepoConfig.cpp \
            $(SRCDIR)/GestorGit.cpp \
            $(SRCDIR)/GtkInterface.cpp

OBJECTS  := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))

# Archivo de secretos
SECRET_HEADER := $(SRCDIR)/PasswordObfuscator_Secrets.h

##############################################################################
# TARGET: all (default)
##############################################################################
.PHONY: all
all: pre-build $(TARGET)

##############################################################################
# TARGET: pre-build - Genera los secretos únicos antes de compilar
##############################################################################
.PHONY: pre-build
pre-build:
	@echo "═══════════════════════════════════════════════════════════════════"
	@echo "🔐 GithubManager V1 - Pre-build: Generando secretos de ofuscación"
	@echo "═══════════════════════════════════════════════════════════════════"
	@mkdir -p $(SRCDIR) $(OBJDIR)
	
	@echo "/* Archivo generado automáticamente en compilación */" > $(SECRET_HEADER)
	@echo "#ifndef PASSWORDOBFUSCATOR_SECRETS_H_" >> $(SECRET_HEADER)
	@echo "#define PASSWORDOBFUSCATOR_SECRETS_H_" >> $(SECRET_HEADER)
	#@echo "" >> $(SECRET_HEADER)
	#@echo "/* Secreto para ofuscación del campo URL */" >> $(SECRET_HEADER)
	
	-@VALUE=$$(od -An -N4 -tu4 /dev/urandom | awk '{print $$1 % 80001 + 10000}'); \
	echo "#define SECRET_NUMBER_URL $$VALUE" >> $(SECRET_HEADER); \
	#echo "  ✓ SECRET_NUMBER_URL = $$VALUE"
	
	#@echo "" >> $(SECRET_HEADER)
	#@echo "/* Secreto para ofuscación del campo TOKEN */" >> $(SECRET_HEADER)
	
	-@VALUE=$$(od -An -N4 -tu4 /dev/urandom | awk '{print $$1 % 800001 + 100000}'); \
	echo "#define SECRET_NUMBER_TOKEN $$VALUE" >> $(SECRET_HEADER); \
	#echo "  ✓ SECRET_NUMBER_TOKEN = $$VALUE"
	
	#@echo "" >> $(SECRET_HEADER)
	#@echo "/* Secreto para ofuscación del campo BRANCH */" >> $(SECRET_HEADER)
	
	-@VALUE=$$(od -An -N4 -tu4 /dev/urandom | awk '{print $$1 % 8000001 + 1000000}'); \
	echo "#define SECRET_NUMBER_BRANCH $$VALUE" >> $(SECRET_HEADER); \
	#echo "  ✓ SECRET_NUMBER_BRANCH = $$VALUE"
	
	@echo "" >> $(SECRET_HEADER)
	@echo "#endif /* PASSWORDOBFUSCATOR_SECRETS_H_ */" >> $(SECRET_HEADER)
	@echo ""
	#@echo "✅ Secretos generados en: $(SECRET_HEADER)"

##############################################################################
# Reglas de compilación
##############################################################################
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp $(SECRET_HEADER)
	@mkdir -p $(OBJDIR)
	@echo "Compilando: $<"
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	@echo ""
	@echo "Enlazando: $(TARGET)"
	$(CC) $(OBJECTS) $(LDFLAGS) -o $(TARGET)
	@echo "✨ Compilación completada: $(TARGET)"

##############################################################################
# Targets utilitarios
##############################################################################
.PHONY: clean clean-secrets rebuild rebuild-secrets show-secrets info help

clean:
	rm -rf $(OBJDIR) $(TARGET)
	-rm -f $(SECRET_HEADER)

clean-secrets:
	-rm -f $(SECRET_HEADER)

rebuild: clean all

rebuild-secrets: clean-secrets all

show-secrets:
	@echo "Secretos de Ofuscación Actuales:"
	@grep "define SECRET_NUMBER" $(SECRET_HEADER) || echo "⚠️ No hay secretos generados aún"

info:
	@echo "Compilador: $(CC)"
	@echo "Target: $(TARGET)"
	@echo "Directorios: $(SRCDIR) / $(OBJDIR)"

help:
	@echo "Targets disponibles: all, rebuild, rebuild-secrets, show-secrets, clean, clean-secrets"
	
	
##############################################################################
# TARGET: post-build - Limpia valores de secretos dejando encabezado vacío
##############################################################################

.PHONY: post-build

$(TARGET): $(OBJECTS) post-build
	@echo ""
	@echo "Enlazando: $(TARGET)"
	$(CC) $(OBJECTS) $(LDFLAGS) -o $(TARGET)
	@echo "✨ Compilación completada: $(TARGET)"

post-build:
	@echo "🔒 Limpiando secretos del sistema de archivos..."
	@if [ -f "$(SECRET_HEADER)" ]; then \
		echo "/* Archivo generado automáticamente en compilación */" > $(SECRET_HEADER); \
		echo "#ifndef PASSWORDOBFUSCATOR_SECRETS_H_" >> $(SECRET_HEADER); \
		echo "#define PASSWORDOBFUSCATOR_SECRETS_H_" >> $(SECRET_HEADER); \
		echo "" >> $(SECRET_HEADER); \
		echo "#endif /* PASSWORDOBFUSCATOR_SECRETS_H_ */" >> $(SECRET_HEADER); \
		echo "✅ Secretos eliminados. Encabezado restaurado."; \
	else \
		echo "⚠️ No hubo secretos para limpiar"; \
	fi

