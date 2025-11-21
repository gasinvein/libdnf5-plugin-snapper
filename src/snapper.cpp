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

std::string join_strings(const std::vector<std::string>& vector, const std::string& separator)
{
    std::string out;

    auto is_first_item = true;
    for (auto & str : vector) {
        if (!is_first_item) {
            out += separator;
        }
        is_first_item = false;
        out += str;
    }

    return out;
}

std::vector<std::string> find_names_in_transaction(const libdnf5::base::Transaction & transaction, const std::vector<std::string> & package_names) {
    std::vector<std::string> out;

    for (auto & pkg_action : transaction.get_transaction_packages()) {
        auto found_name = std::find(package_names.begin(), package_names.end(), pkg_action.get_package().get_name());
        if (found_name != package_names.end()) {
            out.push_back(pkg_action.get_package().get_full_nevra());
            continue;
        }

        for (auto provided : pkg_action.get_package().get_provides()) {
            auto found_provided = std::find(package_names.begin(), package_names.end(), provided.get_name());
            if (found_provided != package_names.end()) {
                out.push_back(pkg_action.get_package().get_full_nevra());
                break;
            }
        }
    }

    return out;
}

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

        auto protected_are_important = libdnf5::OptionBool(DEFAULT_PROTECTED_ARE_IMPORTANT)
                                             .from_string(config.get_value("snapper", "protected_are_important"));
        auto installonly_are_important = libdnf5::OptionBool(DEFAULT_INSTALLONLY_ARE_IMPORTANT)
                                               .from_string(config.get_value("snapper", "installonly_are_important"));

        if (protected_are_important) {
            auto affected_protected_pkgs = find_names_in_transaction(transaction,
                                                                     get_base().get_config().get_protected_packages_option().get_value());
            if (!affected_protected_pkgs.empty()) {
                get_base().get_logger()->info("Snapper plugin: Affected protected packages: {}",
                                              join_strings(affected_protected_pkgs, " "));
                scd.userdata["important"] = "yes";
            }
        }
        if (installonly_are_important) {
            auto affected_installonly_pkgs = find_names_in_transaction(transaction,
                                                                       get_base().get_config().get_installonlypkgs_option().get_value());
            if (!affected_installonly_pkgs.empty()) {
                get_base().get_logger()->info("Snapper plugin: Affected installonly packages: {}",
                                              join_strings(affected_installonly_pkgs, " "));
                scd.userdata["important"] = "yes";
            }
        }

        scd.cleanup = "number";

        std::map<libdnf5::transaction::TransactionItemAction, int> actions_count;

        for (const auto & pkg_action: transaction.get_transaction_packages()) {
            if ((pkg_action.get_action() == libdnf5::transaction::TransactionItemAction::REPLACED) ||
                (pkg_action.get_action() == libdnf5::transaction::TransactionItemAction::REASON_CHANGE)) {
                continue;
            }

            ++actions_count[pkg_action.get_action()];
        }

        std::vector<std::string> actions_count_strings;

        for (const auto & [action, count] : actions_count)
            actions_count_strings.push_back(std::format("{} {}", libdnf5::transaction::transaction_item_action_to_string(action), count));

        scd.description = std::format("DNF ({})", join_strings(actions_count_strings, ", "));

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
