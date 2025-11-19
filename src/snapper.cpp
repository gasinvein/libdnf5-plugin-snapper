#include <cassert>
#include <libdnf5/base/base.hpp>
#include <libdnf5/plugin/iplugin.hpp>

#include <snapper/Snapper.h>
#include <snapper/Snapshot.h>

#include "config.hpp"

namespace {

constexpr const char * PLUGIN_NAME = "snapper";
constexpr libdnf5::plugin::Version PLUGIN_VERSION {VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO};
constexpr libdnf5::PluginAPIVersion REQUIRED_PLUGIN_API_VERSION {.major = 2, .minor = 0};
    
constexpr const char * attrs[] {
    "author.name", "author.email", "description", nullptr
};
constexpr const char * attrs_value[] {
    "Sergei von Alis", "gasinvein@gmail.com", "Snapper Plugin."
};


class SnapperPlugin : public libdnf5::plugin::IPlugin {
public:
    SnapperPlugin(libdnf5::plugin::IPluginData & data, libdnf5::ConfigParser & config) : IPlugin(data), config(config) {}
    virtual ~SnapperPlugin() = default;

    libdnf5::PluginAPIVersion get_api_version() const noexcept override { return REQUIRED_PLUGIN_API_VERSION; }

    const char * get_name() const noexcept override { return PLUGIN_NAME; }

    libdnf5::plugin::Version get_version() const noexcept override { return PLUGIN_VERSION; }

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
        std::string snapper_config_name;
        if (config.has_option("snapper", "config")) {
            snapper_config_name = config.get_value("snapper", "config");
        } else {
            snapper_config_name = "root";
        }
        try {
            snpr.emplace(snapper_config_name, get_base().get_config().get_installroot_option().get_value_string());
            get_base().get_logger()->info("Snapper plugin: using config \"{}\" at {}", snpr->configName(), snpr->subvolumeDir());
        } catch (snapper::ConfigNotFoundException & exception) {
            get_base().get_logger()->warning("Snapper plugin: failed to init: {}", exception.what());
        }
    }

    void pre_transaction(const libdnf5::base::Transaction & transaction) override {
        snapper::Plugins::Report report;
        auto scd = get_transaction_scd(transaction);
        if (snpr) {
            get_base().get_logger()->debug("Snapper plugin: creating pre snapshot");
            pre_snapshot = snpr->createPreSnapshot(scd, report);
            get_base().get_logger()->info("Snapper plugin: created pre snapshot {}", pre_snapshot->getNum());
        } else {
            get_base().get_logger()->info("Snapper plugin: no Snapper - not creating pre snapshot");
        }
    }

    void post_transaction(const libdnf5::base::Transaction & transaction) override {
        snapper::Plugins::Report report;
        auto scd = get_transaction_scd(transaction);
        if (snpr) {
            get_base().get_logger()->debug("Snapper plugin: creating post snapshot");
            post_snapshot = snpr->createPostSnapshot(pre_snapshot, scd, report);
            get_base().get_logger()->info("Snapper plugin: created post snapshot {}", post_snapshot->getNum());
        } else {
            get_base().get_logger()->info("Snapper plugin: no Snapper - not creating post snapshot");
        }
    }

private:
    libdnf5::ConfigParser & config;
    std::optional<snapper::Snapper> snpr;
    snapper::Snapshots::iterator pre_snapshot;
    snapper::Snapshots::iterator post_snapshot;

    snapper::SCD get_transaction_scd(const libdnf5::base::Transaction & transaction) {
        snapper::SCD scd;

        std::vector<std::string> protected_pkgs;
        std::vector<std::string> installonly_pkgs;

        auto protected_are_important = libdnf5::OptionBool(DEFAULT_PROTECTED_ARE_IMPORTANT)
                                             .from_string(config.get_value("snapper", "protected_are_important"));
        auto installonly_are_important = libdnf5::OptionBool(DEFAULT_INSTALLONLY_ARE_IMPORTANT)
                                               .from_string(config.get_value("snapper", "installonly_are_important"));

        if (protected_are_important)
            protected_pkgs = get_base().get_config().get_protected_packages_option().get_value();
        if (installonly_are_important)
            installonly_pkgs = get_base().get_config().get_installonlypkgs_option().get_value();

        scd.cleanup = "number";

        std::map<libdnf5::transaction::TransactionItemAction, int> actions_count;

        for (const auto & pkg_action: transaction.get_transaction_packages()) {
            if ((pkg_action.get_action() == libdnf5::transaction::TransactionItemAction::REPLACED) ||
                (pkg_action.get_action() == libdnf5::transaction::TransactionItemAction::REASON_CHANGE)) {
                continue;
            }

            ++actions_count[pkg_action.get_action()];

            auto pkg_name = pkg_action.get_package().get_name();

            if ((protected_are_important) &&
                (std::find(protected_pkgs.begin(), protected_pkgs.end(), pkg_name) != protected_pkgs.end()))
                scd.userdata["important"] = "yes";
            if ((installonly_are_important) && 
                (std::find(installonly_pkgs.begin(), installonly_pkgs.end(), pkg_name) != installonly_pkgs.end()))
                scd.userdata["important"] = "yes";
        }

        std::string actions_summary;

        bool is_first_item = true;
        for (const auto & [action, count] : actions_count) {
            if (!is_first_item) {
                actions_summary += ", ";
            }
            is_first_item = false;
            actions_summary += std::format("{} {}", libdnf5::transaction::transaction_item_action_to_string(action), count);
        }

        scd.description = std::format("DNF ({})", actions_summary);

        return scd;
    }
};

std::exception_ptr last_exception;

}  // namespace

libdnf5::PluginAPIVersion libdnf_plugin_get_api_version(void) {
    return REQUIRED_PLUGIN_API_VERSION;
}

const char * libdnf_plugin_get_name(void) {
    return PLUGIN_NAME;
}

libdnf5::plugin::Version libdnf_plugin_get_version(void) {
    return PLUGIN_VERSION;
}

libdnf5::plugin::IPlugin * libdnf_plugin_new_instance(
    [[maybe_unused]] libdnf5::LibraryVersion library_version,
    libdnf5::plugin::IPluginData & data,
    libdnf5::ConfigParser & parser) try {
    return new SnapperPlugin(data, parser);
} catch (...) {
    last_exception = std::current_exception();
    return nullptr;
}

void libdnf_plugin_delete_instance(libdnf5::plugin::IPlugin * plugin_object) {
    delete plugin_object;
}

std::exception_ptr * libdnf_plugin_get_last_exception(void) {
    return &last_exception;
}
