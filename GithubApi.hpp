/*
 * GithubApi.hpp
 *
 * Cliente mínimo de la API REST de GitHub mediante OpenSSL.
 * No utiliza curl ni libcurl.
 */

#ifndef GITHUBAPI_HPP_
#define GITHUBAPI_HPP_

#include <string>

class GithubApi {
public:
	struct Resultado {
		bool exito = false;
		int codigoHttp = 0;
		std::string mensaje;
		std::string respuesta;
	};

	/*
	 * Elimina una rama remota en GitHub.
	 *
	 * urlRepo: https://github.com/usuario/repositorio.git
	 * rama:    perfil-r-islas-union-find
	 * token:   Personal Access Token de GitHub
	 */
	static Resultado eliminarRamaRemota(const std::string &urlRepo, const std::string &rama, const std::string &token);

	/*
	 * Extrae usuario y repositorio de una URL de GitHub.
	 *
	 * https://github.com/usuario/repositorio.git
	 * -> usuario, repositorio
	 */
	static bool extraerOwnerRepo(const std::string &urlRepo, std::string &owner, std::string &repo);

	static std::string construirUrlApi(const std::string &owner, const std::string &repo, const std::string &rama);

private:
	static std::string codificarComponenteURL(const std::string &texto);


	static Resultado peticionHttpsDelete(const std::string &host, const std::string &ruta, const std::string &token);
};

#endif /* GITHUBAPI_HPP_ */
