#include <libdnf5/base/base.hpp>
#include <libdnf5/base/transaction.hpp>
#include <libdnf5/conf/const.hpp>
#include <libdnf5/plugin/iplugin.hpp>

#include <snapper/Snapper.h>
#include <snapper/Snapshot.h>

#include "config.hpp"

using namespace libdnf5;

namespace {

constexpr const char * PLUGIN_NAME = "snapper";
constexpr plugin::Version PLUGIN_VERSION {VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO};
constexpr PluginAPIVersion REQUIRED_PLUGIN_API_VERSION {.major = 2, .minor = 0};
    
constexpr const char * attrs[] {
    "author.name", "author.email", "description", nullptr
};
constexpr const char * attrs_value[] {
    "Sergei von Alis", "gasinvein@gmail.com", "Snapper Plugin."
};


class SnapperPlugin : public plugin::IPlugin {
public:
    SnapperPlugin(libdnf5::plugin::IPluginData & data, libdnf5::ConfigParser &) : IPlugin(data) {}
    virtual ~SnapperPlugin() = default;

    PluginAPIVersion get_api_version() const noexcept override { return REQUIRED_PLUGIN_API_VERSION; }

    const char * get_name() const noexcept override { return PLUGIN_NAME; }

    plugin::Version get_version() const noexcept override { return PLUGIN_VERSION; }

    const char * const * get_attributes() const noexcept override { return attrs; }

    const char * get_attribute(const char * attribute) const noexcept override {
        for (size_t i = 0; attrs[i]; ++i) {
            if (std::strcmp(attribute, attrs[i]) == 0) {
                return attrs_value[i];
            }
        }
        return nullptr;
    }

    void init() override {
        const auto & dnf_config = get_base().get_config();
        auto & logger = *get_base().get_logger();
        try {
            snpr = new snapper::Snapper("root", dnf_config.get_installroot_option().get_value_string());
            logger.info("Snapper plugin: using config \"{}\" at {}", snpr->configName(), snpr->subvolumeDir());
        } catch (snapper::ConfigNotFoundException & exception) {
            logger.warning("Snapper plugin: failed to init: {}", exception.what());
        }
    }

    void pre_transaction(const libdnf5::base::Transaction & transaction) override {
        auto & logger = *get_base().get_logger();
        snapper::Plugins::Report report;
        auto scd = get_scd(transaction);
        if (snpr) {
            logger.debug("Snapper plugin: creating pre snapshot");
            pre_snapshot = snpr->createPreSnapshot(scd, report);
            logger.info("Snapper plugin: created pre snapshot {}", pre_snapshot->getNum());
        } else {
            logger.info("Snapper plugin: no Snapper - not creating pre snapshot");
        }
    }

    void post_transaction(const libdnf5::base::Transaction & transaction) override {
        auto & logger = *get_base().get_logger();
        snapper::Plugins::Report report;
        auto scd = get_scd(transaction);
        if (snpr) {
            logger.debug("Snapper plugin: creating post snapshot");
            post_snapshot = snpr->createPostSnapshot(pre_snapshot, scd, report);
            logger.info("Snapper plugin: created post snapshot {}", post_snapshot->getNum());
        } else {
            logger.info("Snapper plugin: no Snapper - not creating post snapshot");
        }
    }

private:
    snapper::Snapper * snpr = nullptr;
    snapper::Snapshots::iterator pre_snapshot;
    snapper::Snapshots::iterator post_snapshot;

    snapper::SCD get_scd(const libdnf5::base::Transaction & transaction) {
        snapper::SCD scd;
        scd.cleanup = "number";
        scd.description = std::format("DNF transaction ({} package actions)", transaction.get_transaction_packages_count());
        for (auto pkg_action: transaction.get_transaction_packages()) {
            auto key = pkg_action.get_package().to_string();
            auto value = libdnf5::transaction::transaction_item_action_to_string(pkg_action.get_action());
            scd.userdata.insert({key, value});
        }
        return scd;
    }
};

std::exception_ptr last_exception;

}  // namespace

PluginAPIVersion libdnf_plugin_get_api_version(void) {
    return REQUIRED_PLUGIN_API_VERSION;
}

const char * libdnf_plugin_get_name(void) {
    return PLUGIN_NAME;
}

plugin::Version libdnf_plugin_get_version(void) {
    return PLUGIN_VERSION;
}

plugin::IPlugin * libdnf_plugin_new_instance(
    [[maybe_unused]] LibraryVersion library_version,
    libdnf5::plugin::IPluginData & data,
    libdnf5::ConfigParser & parser) try {
    return new SnapperPlugin(data, parser);
} catch (...) {
    last_exception = std::current_exception();
    return nullptr;
}

void libdnf_plugin_delete_instance(plugin::IPlugin * plugin_object) {
    delete plugin_object;
}

std::exception_ptr * libdnf_plugin_get_last_exception(void) {
    return &last_exception;
}
