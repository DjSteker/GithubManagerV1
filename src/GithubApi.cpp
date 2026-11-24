/*
 * GithubApi.cpp
 *
 * Cliente mínimo de la API REST de GitHub mediante OpenSSL.
 * No utiliza curl ni libcurl.
 */

#include "GithubApi.hpp"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <cctype>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace {

std::string minusculas(const std::string &texto) {
	std::string resultado = texto;

	for (char &c : resultado) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}

	return resultado;
}

std::string recortar(const std::string &texto) {
	size_t inicio = 0;
	size_t fin = texto.size();

	while (inicio < fin && std::isspace(static_cast<unsigned char>(texto[inicio]))) {
		++inicio;
	}

	while (fin > inicio && std::isspace(static_cast<unsigned char>(texto[fin - 1]))) {
		--fin;
	}

	return texto.substr(inicio, fin - inicio);
}

bool empiezaPor(const std::string &texto, const std::string &prefijo) {
	return texto.size() >= prefijo.size() && texto.compare(0, prefijo.size(), prefijo) == 0;
}

bool terminaPor(const std::string &texto, const std::string &sufijo) {
	return texto.size() >= sufijo.size() && texto.compare(texto.size() - sufijo.size(), sufijo.size(), sufijo) == 0;
}

} // namespace

std::string GithubApi::codificarComponenteURL(const std::string &texto) {
	static const char *hex = "0123456789ABCDEF";

	std::ostringstream salida;

	for (unsigned char c : texto) {
		const bool seguro = std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';

		if (seguro) {
			salida << static_cast<char>(c);
		} else {
			salida << '%' << hex[(c >> 4) & 0x0F] << hex[c & 0x0F];
		}
	}

	return salida.str();
}

bool GithubApi::extraerOwnerRepo(const std::string &urlRepo, std::string &owner, std::string &repo) {
	std::string url = urlRepo;

	if (empiezaPor(url, "https://")) {
		url.erase(0, 8);
	} else if (empiezaPor(url, "http://")) {
		url.erase(0, 7);
	}

	if (empiezaPor(url, "www.")) {
		url.erase(0, 4);
	}

	if (!empiezaPor(url, "github.com/")) {
		return false;
	}

	url.erase(0, 11);

	if (terminaPor(url, ".git")) {
		url.erase(url.size() - 4);
	}

	// Elimina barra final, si existe.
	while (!url.empty() && url.back() == '/') {
		url.pop_back();
	}

	const size_t barra = url.find('/');

	if (barra == std::string::npos || barra == 0) {
		return false;
	}

	owner = url.substr(0, barra);
	repo = url.substr(barra + 1);

	// Elimina rutas extra, por ejemplo: usuario/repo/tree/main
	const size_t segundaBarra = repo.find('/');

	if (segundaBarra != std::string::npos) {
		repo = repo.substr(0, segundaBarra);
	}

	if (terminaPor(repo, ".git")) {
		repo.erase(repo.size() - 4);
	}

	return !owner.empty() && !repo.empty();
}

std::string GithubApi::construirUrlApi(const std::string &owner, const std::string &repo, const std::string &rama) {
	return "https://api.github.com/repos/" + codificarComponenteURL(owner) + "/" + codificarComponenteURL(repo) + "/git/refs/heads/" + codificarComponenteURL(rama);
}

GithubApi::Resultado GithubApi::peticionHttpsDelete(const std::string &host, const std::string &ruta, const std::string &token) {
	Resultado resultado;

	SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());

	if (!ctx) {
		resultado.mensaje = "ERROR: no se pudo crear el contexto SSL de OpenSSL.";
		return resultado;
	}

	SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

	// Verificación estándar del certificado del servidor.
	SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);

	if (!SSL_CTX_set_default_verify_paths(ctx)) {
		SSL_CTX_free(ctx);
		resultado.mensaje = "ERROR: no se pudieron cargar los certificados CA del sistema.";
		return resultado;
	}

	BIO *bio = BIO_new_ssl_connect(ctx);

	if (!bio) {
		SSL_CTX_free(ctx);
		resultado.mensaje = "ERROR: no se pudo crear la conexión SSL.";
		return resultado;
	}

	SSL *ssl = nullptr;
	BIO_get_ssl(bio, &ssl);

	if (!ssl) {
		BIO_free_all(bio);
		SSL_CTX_free(ctx);
		resultado.mensaje = "ERROR: no se pudo obtener el objeto SSL.";
		return resultado;
	}

	SSL_set_tlsext_host_name(ssl, host.c_str());

	const std::string destino = host + ":443";
	BIO_set_conn_hostname(bio, destino.c_str());

	if (BIO_do_connect(bio) <= 0) {
		unsigned long error = ERR_get_error();
		char buffer[256] = { 0 };
		ERR_error_string_n(error, buffer, sizeof(buffer));

		resultado.mensaje = std::string("ERROR: no se pudo conectar con ") + destino + ". Detalle: " + buffer;

		BIO_free_all(bio);
		SSL_CTX_free(ctx);
		return resultado;
	}

	if (BIO_do_handshake(bio) <= 0) {
		unsigned long error = ERR_get_error();
		char buffer[256] = { 0 };
		ERR_error_string_n(error, buffer, sizeof(buffer));

		resultado.mensaje = std::string("ERROR: fallo en el handshake TLS. Detalle: ") + buffer;

		BIO_free_all(bio);
		SSL_CTX_free(ctx);
		return resultado;
	}

	// Comprueba que el certificado corresponde al host solicitado.
	if (SSL_get_verify_result(ssl) != X509_V_OK) {
		resultado.mensaje = "ERROR: la verificación del certificado TLS ha fallado.";

		BIO_free_all(bio);
		SSL_CTX_free(ctx);
		return resultado;
	}

	std::ostringstream peticion;
	peticion << "DELETE " << ruta << " HTTP/1.1\r\n" << "Host: " << host << "\r\n" << "User-Agent: GestorGit\r\n" << "Accept: application/vnd.github+json\r\n" << "X-GitHub-Api-Version: 2022-11-28\r\n"
			<< "Authorization: token " << token << "\r\n" << "Connection: close\r\n" << "Content-Length: 0\r\n" << "\r\n";

	const std::string peticionStr = peticion.str();

	const size_t escritos = BIO_write(bio, peticionStr.data(), static_cast<int>(peticionStr.size()));

	if (escritos != peticionStr.size()) {
		resultado.mensaje = "ERROR: no se pudo enviar la petición HTTPS completa.";

		BIO_free_all(bio);
		SSL_CTX_free(ctx);
		return resultado;
	}

	std::string respuesta;
	char buffer[4096];
	int leidos = 0;

	while ((leidos = BIO_read(bio, buffer, sizeof(buffer))) > 0) {
		respuesta.append(buffer, static_cast<size_t>(leidos));
	}

	BIO_free_all(bio);
	SSL_CTX_free(ctx);

	if (respuesta.empty()) {
		resultado.mensaje = "ERROR: el servidor no devolvió ninguna respuesta.";
		return resultado;
	}

	// Extrae el código HTTP de la primera línea.
	const size_t finPrimeraLinea = respuesta.find("\r\n");

	if (finPrimeraLinea == std::string::npos) {
		resultado.mensaje = "ERROR: respuesta HTTP incompleta.";
		resultado.respuesta = respuesta;
		return resultado;
	}

	const std::string primeraLinea = respuesta.substr(0, finPrimeraLinea);

	const size_t espacio1 = primeraLinea.find(' ');
	const size_t espacio2 = primeraLinea.find(' ', espacio1 + 1);

	if (espacio1 == std::string::npos || espacio2 == std::string::npos) {
		resultado.mensaje = "ERROR: no se pudo interpretar la línea de estado HTTP.";
		resultado.respuesta = respuesta;
		return resultado;
	}

	try {
		resultado.codigoHttp = std::stoi(primeraLinea.substr(espacio1 + 1));
	} catch (...) {
		resultado.mensaje = "ERROR: código HTTP no válido en la respuesta.";
		resultado.respuesta = respuesta;
		return resultado;
	}

	// Separa cabeceras y cuerpo.
	const size_t finCabeceras = respuesta.find("\r\n\r\n");

	if (finCabeceras != std::string::npos) {
		resultado.respuesta = respuesta.substr(finCabeceras + 4);
	} else {
		resultado.respuesta.clear();
	}

	if (resultado.codigoHttp == 204) {
		resultado.exito = true;
		resultado.mensaje = "Rama remota eliminada correctamente.";
	} else if (resultado.codigoHttp == 404) {
		resultado.mensaje = "La rama remota no existe o la URL es incorrecta.";
	} else if (resultado.codigoHttp == 401) {
		resultado.mensaje = "Token inválido, caducado o sin permisos suficientes.";
	} else if (resultado.codigoHttp == 403) {
		resultado.mensaje = "Acceso denegado. Revisa los permisos del token.";
	} else {
		resultado.mensaje = "GitHub devolvió el código HTTP " + std::to_string(resultado.codigoHttp) + ".";
	}

	return resultado;
}

GithubApi::Resultado GithubApi::eliminarRamaRemota(const std::string &urlRepo, const std::string &rama, const std::string &token) {
	Resultado resultado;

	if (urlRepo.empty()) {
		resultado.mensaje = "La URL del repositorio está vacía.";
		return resultado;
	}

	if (rama.empty()) {
		resultado.mensaje = "El nombre de la rama está vacío.";
		return resultado;
	}

	if (token.empty()) {
		resultado.mensaje = "El token de GitHub está vacío.";
		return resultado;
	}

	std::string owner;
	std::string repo;

	if (!extraerOwnerRepo(urlRepo, owner, repo)) {
		resultado.mensaje = "No se pudo extraer usuario y repositorio de la URL.\n"
				"Formato esperado: https://github.com/usuario/repositorio.git";
		return resultado;
	}

	const std::string host = "api.github.com";
	const std::string ruta = "/repos/" + codificarComponenteURL(owner) + "/" + codificarComponenteURL(repo) + "/git/refs/heads/" + codificarComponenteURL(rama);

	return peticionHttpsDelete(host, ruta, token);
}
