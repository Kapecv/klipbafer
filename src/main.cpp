#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include "ClipManager.hpp"
#include <Windows.h>

using namespace geode::prelude;

class ClipsPopup : public geode::Popup<> {
protected:
    bool setup() override {
        this->setTitle("Moje klipy");
        auto win = m_mainLayer->getContentSize();

        auto bg = CCScale9Sprite::create("square02b_001.png", { 0, 0, 80, 80 });
        bg->setColor({ 0, 0, 0 });
        bg->setOpacity(100);
        bg->setContentSize({ 340.f, 180.f });
        bg->setPosition({ win.width / 2, win.height / 2 - 8.f });
        m_mainLayer->addChild(bg);

        std::vector<fs::path> clips;
        std::error_code ec;
        for (auto& e : fs::directory_iterator(ClipManager::get().clipsDir(), ec)) {
            if (e.path().extension() == ".mp4") clips.push_back(e.path());
        }
        std::sort(clips.begin(), clips.end(), [&](auto& a, auto& b) {
            std::error_code e1;
            return fs::last_write_time(a, e1) > fs::last_write_time(b, e1);
        });

        auto scroll = ScrollLayer::create({ 340.f, 180.f });
        scroll->setPosition({ win.width / 2 - 170.f, win.height / 2 - 98.f });
        m_mainLayer->addChild(scroll);

        if (clips.empty()) {
            auto lbl = CCLabelBMFont::create("Brak klipow. Zapisz pierwszy z menu pauzy!", "bigFont.fnt");
            lbl->setScale(0.4f);
            lbl->setPosition({ win.width / 2, win.height / 2 - 8.f });
            m_mainLayer->addChild(lbl);
        }

        auto content = scroll->m_contentLayer;
        content->setLayout(
            ColumnLayout::create()
                ->setAxisReverse(true)
                ->setAxisAlignment(AxisAlignment::End)
                ->setAutoGrowAxis(180.f)
                ->setGap(6.f)
        );
        for (auto& c : clips) content->addChild(makeCell(c));
        content->updateLayout();

        auto menu = CCMenu::create();
        menu->setPosition({ 0, 0 });
        m_mainLayer->addChild(menu);

        auto folderSpr = ButtonSprite::create("Otworz folder", "goldFont.fnt", "GJ_button_04.png", 0.6f);
        auto folderBtn = CCMenuItemExt::createSpriteExtra(folderSpr, [](auto*) {
            utils::file::openFolder(ClipManager::get().clipsDir());
        });
        folderBtn->setPosition({ win.width / 2, 14.f });
        menu->addChild(folderBtn);

        return true;
    }

    CCNode* makeCell(fs::path const& path) {
        auto cell = CCNode::create();
        cell->setContentSize({ 340.f, 40.f });

        auto bg = CCScale9Sprite::create("square02b_001.png", { 0, 0, 80, 80 });
        bg->setColor({ 40, 90, 160 });
        bg->setOpacity(130);
        bg->setContentSize({ 334.f, 38.f });
        bg->setPosition({ 170.f, 20.f });
        cell->addChild(bg);

        std::error_code ec;
        double mb = fs::file_size(path, ec) / (1024.0 * 1024.0);

        auto name = CCLabelBMFont::create(utils::string::pathToString(path.stem()).c_str(), "bigFont.fnt");
        name->setScale(0.35f);
        name->limitLabelWidth(190.f, 0.35f, 0.1f);
        name->setAnchorPoint({ 0.f, 0.5f });
        name->setPosition({ 12.f, 26.f });
        cell->addChild(name);

        auto info = CCLabelBMFont::create(fmt::format("{:.1f} MB", mb).c_str(), "chatFont.fnt");
        info->setScale(0.6f);
        info->setColor({ 200, 200, 200 });
        info->setAnchorPoint({ 0.f, 0.5f });
        info->setPosition({ 12.f, 11.f });
        cell->addChild(info);

        auto menu = CCMenu::create();
        menu->setPosition({ 0, 0 });
        cell->addChild(menu);

        auto playSpr = ButtonSprite::create("Odtworz", "goldFont.fnt", "GJ_button_01.png", 0.6f);
        auto playBtn = CCMenuItemExt::createSpriteExtra(playSpr, [path](auto*) {
            ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        });
        playBtn->setPosition({ 265.f, 20.f });
        menu->addChild(playBtn);

        auto delSpr = CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png");
        delSpr->setScale(0.6f);
        auto delBtn = CCMenuItemExt::createSpriteExtra(delSpr, [this, path](auto*) {
            createQuickPopup("Usun klip", "Na pewno usunac ten klip?", "Nie", "Tak",
                [this, path](FLAlertLayer*, bool yes) {
                    if (!yes) return;
                    std::error_code e;
                    fs::remove(path, e);
                    this->onClose(nullptr);
                    ClipsPopup::create()->show();
                });
        });
        delBtn->setPosition({ 318.f, 20.f });
        menu->addChild(delBtn);

        return cell;
    }

public:
    static ClipsPopup* create() {
        auto ret = new ClipsPopup();
        if (ret->initAnchored(380.f, 260.f)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

class $modify(ClipMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto spr = CircleButtonSprite::createWithSpriteFrameName("GJ_replayBtn_001.png");
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [](auto*) {
            ClipsPopup::create()->show();
        });
        btn->setID("clips-button"_spr);

        if (auto menu = this->getChildByID("bottom-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        }
        return true;
    }
};

class $modify(ClipPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        ClipManager::get().start();
        return true;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        ClipManager::get().pushFrame();
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (Mod::get()->getSettingValue<bool>("auto-save-complete")) {
            ClipManager::get().save(m_level->m_levelName);
        }
    }

    void onQuit() {
        ClipManager::get().stop();
        PlayLayer::onQuit();
    }
};

class $modify(ClipPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto spr = ButtonSprite::create("Zapisz klip", "goldFont.fnt", "GJ_button_01.png", 0.7f);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [](auto*) {
            if (auto pl = PlayLayer::get()) {
                ClipManager::get().save(pl->m_level->m_levelName);
            }
        });

        auto menu = CCMenu::create();
        menu->addChild(btn);
        auto win = CCDirector::get()->getWinSize();
        menu->setPosition({ win.width - 70.f, 30.f });
        this->addChild(menu);
    }
};
