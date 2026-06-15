#include "stdafx.h"

#include "component_info.h"
#include "config.h"
#include "foobar_metadata_repository.h"
#include "http/foobar_http_client.h"
#include "service_locator.h"

#include <SDK/initquit.h>

DECLARE_COMPONENT_VERSION(OPENSUBSONIC_COMPONENT_NAME,
						  OPENSUBSONIC_COMPONENT_VERSION,
						  OPENSUBSONIC_COMPONENT_ABOUT);

VALIDATE_COMPONENT_FILENAME(OPENSUBSONIC_COMPONENT_NAME ".dll");

FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE;

class initquit_foo_opensubsonic : public initquit {
  public:
	void on_init() override {
		auto credentials = subsonic::config::load_server_credentials();

		m_http_client = std::make_shared<subsonic::foobar_http_client>(
			std::move(credentials));

		m_metadata_repo =
			std::make_shared<subsonic::foobar_metadata_repository>();

		subsonic::service_locator::initialize(m_http_client, m_metadata_repo);
	}

	void on_quit() override {
		subsonic::service_locator::shutdown();

		m_metadata_repo.reset();
		m_http_client.reset();
	}

  private:
	std::shared_ptr<subsonic::foobar_http_client> m_http_client;
	std::shared_ptr<subsonic::foobar_metadata_repository> m_metadata_repo;
};

static initquit_factory_t<initquit_foo_opensubsonic>
	g_initquit_foo_opensubsonic;
