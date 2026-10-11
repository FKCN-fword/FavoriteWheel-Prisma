#include "Wheel.h"
#include "RuntimeSupport.h"
#include "Outfits.h"
#include "Settings.h"
#include "UIResources.h"
namespace {
    void OnMessage(SKSE::MessagingInterface::Message* message) {
        if (!message) return;
        switch (message->type) {
        case SKSE::MessagingInterface::kPostPostLoad: Wheel::FaceLight::Discover(); break;
        case SKSE::MessagingInterface::kDataLoaded:
            if (Wheel::Config().enabled && (!Wheel::InstallRenderer() || !Wheel::InstallWheel()))
                SKSE::log::error("FavoriteWheel initialization failed. See preceding errors.");
            break;
        case SKSE::MessagingInterface::kPreLoadGame: Wheel::SetGameActive(false); break;
        case SKSE::MessagingInterface::kNewGame: Wheel::SetGameActive(true); break;
        case SKSE::MessagingInterface::kPostLoadGame: Wheel::SetGameActive(message->data != nullptr); break;
        default: break;
        }
    }
}
SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    Wheel::LoadSettings();
    SKSE::log::info("Settings load: {}",Wheel::SettingsDiagnostic());
    Wheel::LoadResources();
    const auto config=Wheel::Config();
    SKSE::log::info("UI resources: {} languages, {} themes; language={} system={} resolved={} theme={}",
        Wheel::Languages().size(),Wheel::Themes().size(),config.language,Wheel::SystemLanguage(),Wheel::ActiveLanguage(config),config.theme);
    const auto runtime=REL::Module::get().version();
    if (!Wheel::RuntimeSupport::Supported(runtime)) {
        SKSE::log::error("FavoriteWheel 0.5.3 unsupported runtime {}; supported runtimes: 1.5.97, 1.6.640, 1.6.1170, GOG 1.6.1179, 1.7.99 and 1.7.104",runtime.string());
        return false;
    }
    SKSE::log::info("FavoriteWheel runtime={} family={}; CommonLibSSE-NG v11.0.0 (94faaed0c60e)",
        runtime.string(),REL::Module::IsAE()?"AE":"SE");
    if(!SKSE::GetSerializationInterface())return false;
    auto messaging = SKSE::GetMessagingInterface();
    if (!messaging || !SKSE::GetTaskInterface() || !messaging->RegisterListener(OnMessage)) return false;
    if(!Wheel::Outfits::Install())return false;
    SKSE::log::info("FavoriteWheel 0.5.3 loaded; categorized favorites/actions; optional full inventory and time behavior; controller and native quick slots");
    return true;
}
