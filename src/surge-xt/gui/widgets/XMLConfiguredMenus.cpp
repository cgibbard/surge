/*
** Surge Synthesizer is Free and Open Source Software
**
** Surge is made available under the Gnu General Public License, v3.0
** https://www.gnu.org/licenses/gpl-3.0.en.html
**
** Copyright 2004-2021 by various individuals as described by the Git transaction log
**
** All source at: https://github.com/surge-synthesizer/surge.git
**
** Surge was a commercial product from 2004-2018, with Copyright and ownership
** in that period held by Claes Johanson at Vember Audio. Claes made Surge
** open source in September 2018.
*/

#include "XMLConfiguredMenus.h"
#include "SurgeStorage.h"
#include "SurgeGUIEditor.h"
#include "SurgeGUIUtils.h"
#include "RuntimeFont.h"
#include "SurgeImage.h"
#include "AccessibleHelpers.h"
#include "MenuCustomComponents.h"

namespace Surge
{
namespace Widgets
{

void XMLMenuPopulator::scanXMLPresets()
{
    TiXmlElement *sect = storage->getSnapshotSection(mtype);

    if (sect)
    {
        TiXmlElement *type = sect->FirstChildElement();

        while (type)
        {
            if (type->Value() && strcmp(type->Value(), "type") == 0)
            {
                scanXMLPresetForType(type, std::vector<std::string>());
            }
            else if (type->Value() && strcmp(type->Value(), "sectionheader") == 0)
            {
                Item sec;
                sec.isSectionHeader = true;
                if (type->Attribute("label"))
                    sec.name = type->Attribute("label");
                else
                    sec.name = "UNLABELED SECTION";
                int b = 0;
                if (type->QueryIntAttribute("columnbreak", &b) != TIXML_SUCCESS)
                    b = 0;
                sec.hasColumnBreak = b;
                allPresets.push_back(sec);
            }
            else if (type->Value() && strcmp(type->Value(), "separator") == 0)
            {
                Item sep;
                sep.isSeparator = true;
                allPresets.push_back(sep);
            }

            type = type->NextSiblingElement();
        }
    }
}

void XMLMenuPopulator::scanXMLPresetForType(TiXmlElement *type,
                                            const std::vector<std::string> &path)
{
    if (!(type->Value() && strcmp(type->Value(), "type") == 0))
        return;

    auto n = type->Attribute("name");
    if (!n)
        return;
    /*
     * OK we have two cases. If it is a type on its own then it is an entry in path.
     * If it has children, then the children are snapshots and are entries in my named path
     */

    if (type->NoChildren())
    {
        Item it;
        it.xmlElement = type;
        it.pathElements = path;
        it.name = n;
        int t = 0;
        if (type->QueryIntAttribute("i", &t) == TIXML_SUCCESS)
            it.itemType = t;
        allPresets.push_back(it);
    }
    else
    {
        auto p = path;
        p.push_back(n);
        int t = 0;
        if (type->QueryIntAttribute("i", &t) != TIXML_SUCCESS)
        {
            // ERROR
            jassert(false);
            std::cout << "INTERNAL MENU ERROR" << std::endl;
            return;
        }
        auto kid = type->FirstChildElement();
        while (kid)
        {
            std::string kval = kid->Value();
            if (kval == "type")
            {
                scanXMLPresetForType(kid, p);
            }
            else if (kval == "snapshot")
            {
                Item it;
                it.xmlElement = kid;
                it.pathElements = p;
                it.itemType = t;
                auto kn = kid->Attribute("name");
                it.name = kn ? kn : "-ERROR-";
                allPresets.push_back(it);
            }
            else
            {
                std::cout << "Wuh? " << kval << std::endl;
            }
            kid = kid->NextSiblingElement();
        }
    }
}

void XMLMenuPopulator::populate()
{
    /*
     * New scanners
     */
    allPresets.clear();
    scanXMLPresets();
    scanExtraPresets();

    struct Tree
    {
        enum
        {
            SEP,
            SECHEAD,
            FACPS,
            USPS,
            FOLD
        } type;

        ~Tree()
        {
            for (auto c : children)
                delete c;
        }

        std::string name;
        std::vector<std::string> fullPath;
        int idx = -1;
        int depth = 0;
        Tree *parent{nullptr};
        std::vector<Tree *> children;
        bool hasUser{false};
        bool hasFac{false};
        bool colBreak{false};

        void addByPath(const Item &i, int idx, int depth = 0)
        {
            if (i.pathElements.size() == depth)
            {
                auto t = new Tree();

                t->name = i.name;
                t->idx = idx;
                t->fullPath = i.pathElements;
                t->parent = this;
                t->depth = depth;
                t->type =
                    i.isSeparator ? SEP : (i.isSectionHeader ? SECHEAD : (i.isUser ? USPS : FACPS));
                t->colBreak = i.hasColumnBreak;

                children.push_back(t);
            }
            else
            {
                Tree *addToThis = nullptr;

                for (auto c : children)
                {
                    if (c->type == FOLD && c->name == i.pathElements[depth])
                    {
                        addToThis = c;
                    }
                }

                if (!addToThis)
                {
                    addToThis = new Tree();
                    addToThis->name = i.pathElements[depth];
                    addToThis->fullPath = std::vector<std::string>(
                        i.pathElements.begin(), i.pathElements.begin() + depth + 1);
                    addToThis->type = FOLD;
                    addToThis->parent = this;
                    addToThis->depth = depth + 1;

                    children.push_back(addToThis);
                }

                addToThis->addByPath(i, idx, depth + 1);
            }
        }

        void updateFacUserFlag()
        {
            hasFac = false;
            hasUser = false;

            for (auto c : children)
            {
                switch (c->type)
                {
                case FOLD:
                {
                    c->updateFacUserFlag();
                    hasFac |= c->hasFac;
                    hasUser |= c->hasUser;
                }
                break;
                case FACPS:
                    hasFac = true;
                    break;
                case USPS:
                    hasUser = true;
                    break;
                case SEP:
                case SECHEAD:
                    break;
                }
            }
        }

        void buildJuceMenu(juce::PopupMenu &m, XMLMenuPopulator *host)
        {
            bool inFac = true;

            if (depth == 1 && hasFac && hasUser)
            {
                m.addSectionHeader("FACTORY PRESETS");
            }

            for (auto c : children)
            {
                // std::cout << c->type << " " << c->name << std::endl;
                switch (c->type)
                {
                case FACPS:
                {
                    auto idx = c->idx;
                    m.addItem(c->name, [host, idx]() { host->loadByIndex(idx); });
                }
                break;
                case USPS:
                {
                    if (inFac && depth == 1 && hasFac && hasUser)
                    {
                        inFac = false;
                        m.addColumnBreak();
                        m.addSectionHeader("USER PRESETS");
                    }
                    auto idx = c->idx;
                    m.addItem(c->name, [host, idx]() { host->loadByIndex(idx); });
                }
                break;
                case SEP:
                {
                    m.addSeparator();
                }
                break;
                case SECHEAD:
                {
                    if (c->colBreak)
                        m.addColumnBreak();
                    m.addSectionHeader(c->name);
                }
                break;
                case FOLD:
                {
                    auto subM = juce::PopupMenu();
                    c->buildJuceMenu(subM, host);
                    m.addSubMenu(c->name, subM);
                }
                break;
                }
            }
        }
    };

    auto rootTree = std::make_unique<Tree>();
    int idx = 0;

    for (const auto &p : allPresets)
    {
        rootTree->addByPath(p, idx);
        idx++;
    }

    rootTree->updateFacUserFlag();
    menu = juce::PopupMenu();

    rootTree->buildJuceMenu(menu, this);

    maxIdx = allPresets.size();
}

OscillatorMenu::OscillatorMenu()
{
    strcpy(mtype, "osc");
    setDescription("Oscillator Type");
    setTitle("Oscillator Type");
}

OscillatorMenu::~OscillatorMenu() = default;

void OscillatorMenu::paint(juce::Graphics &g)
{
    bg->draw(g, 1.0);
    if (isHovered && bgHover)
        bgHover->draw(g, 1.0);

    int i = osc->type.val.i;

    g.setFont(Surge::GUI::getFontManager()->getLatoAtSize(font_size, font_style));

    if (isHovered)
    {
        g.setColour(skin->getColor(Colors::Osc::Type::TextHover));
    }
    else
    {
        g.setColour(skin->getColor(Colors::Osc::Type::Text));
    }

    std::string txt = osc_type_shortnames[i];
    if (text_allcaps)
    {
        std::transform(txt.begin(), txt.end(), txt.begin(), ::toupper);
    }

    auto tr = getLocalBounds().translated(text_hoffset, text_voffset);
    g.drawText(txt, tr, text_align);
}

void OscillatorMenu::loadSnapshot(int type, TiXmlElement *e, int idx)
{
    auto sge = firstListenerOfType<SurgeGUIEditor>();
    if (sge)
    {
        auto sc = sge->current_scene;
        sge->oscilatorMenuIndex[sc][sge->current_osc[sc]] = idx;
    }
    osc->queue_type = type;
    osc->queue_xmldata = e;
}

void OscillatorMenu::mouseDown(const juce::MouseEvent &event)
{
    if (forwardedMainFrameMouseDowns(event))
    {
        return;
    }

    menu.showMenuAsync(juce::PopupMenu::Options(),
                       Surge::GUI::makeAsyncCallback<OscillatorMenu>(this, [](auto *that, int) {
                           that->isHovered = false;
                           that->repaint();
                       }));
}

void OscillatorMenu::mouseWheelMove(const juce::MouseEvent &event,
                                    const juce::MouseWheelDetails &wheel)
{
    int dir = wheelAccumulationHelper.accumulate(wheel, false, true);

    if (dir != 0)
    {
        jogBy(-dir);
    }
}
void OscillatorMenu::mouseEnter(const juce::MouseEvent &event)
{
    isHovered = true;
    repaint();
}

void OscillatorMenu::mouseExit(const juce::MouseEvent &event)
{
    isHovered = false;
    repaint();
}

bool OscillatorMenu::keyPressed(const juce::KeyPress &key)
{
    auto [action, mod] = Surge::Widgets::accessibleEditAction(key, storage);

    if (action == None)
        return false;

    if (action == OpenMenu || action == Return)
    {
        menu.showMenuAsync(juce::PopupMenu::Options());
        return true;
    }

    switch (action)
    {
    case Increase:
        jogBy(+1);
        return true;
        break;
    case Decrease:
        jogBy(-1);
        return true;
        break;
    default:
        return false; // but bail out if it does
        break;
    }

    return false;
}

template <typename T> struct XMLValue
{
    static std::string value(T *comp) { return "ERROR"; }
};

template <> struct XMLValue<OscillatorMenu>
{
    static std::string value(OscillatorMenu *comp)
    {
        int ids = comp->osc->type.val.i;
        return osc_type_names[ids];
    }
};

template <typename T> struct XMLMenuAH : public juce::AccessibilityHandler
{
    struct XMLMenuTextValue : public juce::AccessibilityTextValueInterface
    {
        XMLMenuTextValue(T *c) : comp(c) {}
        T *comp;

        bool isReadOnly() const override { return true; }
        juce::String getCurrentValueAsString() const override { return XMLValue<T>::value(comp); }
        void setValueAsString(const juce::String &newValue) override{};
    };

    explicit XMLMenuAH(T *s)
        : comp(s), juce::AccessibilityHandler(
                       *s, juce::AccessibilityRole::button,
                       juce::AccessibilityActions()
                           .addAction(juce::AccessibilityActionType::showMenu,
                                      [this]() { this->showMenu(); })
                           .addAction(juce::AccessibilityActionType::press,
                                      [this]() { this->showMenu(); }),
                       AccessibilityHandler::Interfaces{std::make_unique<XMLMenuTextValue>(s)})
    {
    }
    void showMenu() { comp->menu.showMenuAsync(juce::PopupMenu::Options()); }

    T *comp;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XMLMenuAH);
};
std::unique_ptr<juce::AccessibilityHandler> OscillatorMenu::createAccessibilityHandler()
{
    return std::make_unique<XMLMenuAH<OscillatorMenu>>(this);
}

FxMenu::FxMenu()
{
    strcpy(mtype, "fx");
    setDescription("FX Type");
    setTitle("FX Type");
}

void FxMenu::paint(juce::Graphics &g)
{
    if (bg)
        bg->draw(g, 1.0);
    if (isHovered && bgHover)
        bgHover->draw(g, 1.0);

    auto fgc = skin->getColor(Colors::Effect::Menu::Text);
    if (isHovered)
        fgc = skin->getColor(Colors::Effect::Menu::TextHover);

    g.setFont(Surge::GUI::getFontManager()->displayFont);
    g.setColour(fgc);
    auto r = getLocalBounds().reduced(2).withTrimmedLeft(4).withTrimmedRight(12);
    g.drawText(fxslot_names[current_fx], r, juce::Justification::centredLeft);
    g.drawText(fx_type_shortnames[fx->type.val.i], r, juce::Justification::centredRight);
}

void FxMenu::mouseDown(const juce::MouseEvent &event)
{
    if (forwardedMainFrameMouseDowns(event))
    {
        return;
    }

    menu.showMenuAsync(juce::PopupMenu::Options(),
                       Surge::GUI::makeAsyncCallback<FxMenu>(this, [](auto *that, int) {
                           that->isHovered = false;
                           that->repaint();
                       }));
}

void FxMenu::mouseEnter(const juce::MouseEvent &event)
{
    isHovered = true;
    repaint();
}

void FxMenu::mouseExit(const juce::MouseEvent &event)
{
    isHovered = false;
    repaint();
}

void FxMenu::loadSnapshot(int type, TiXmlElement *e, int idx)
{
    if (type > -1)
    {
        fxbuffer->type.val.i = type;

        Effect *t_fx = spawn_effect(type, storage, fxbuffer, 0);
        if (t_fx)
        {
            t_fx->init_ctrltypes();
            t_fx->init_default_values();
            delete t_fx;
        }

        if (e)
        {
            // storage->patch.update_controls();
            selectedName = e->Attribute("name");

            for (int i = 0; i < n_fx_params; i++)
            {
                double d;
                int j;
                char lbl[TXT_SIZE], sublbl[TXT_SIZE];
                snprintf(lbl, TXT_SIZE, "p%i", i);
                if (fxbuffer->p[i].valtype == vt_float)
                {
                    if (e->QueryDoubleAttribute(lbl, &d) == TIXML_SUCCESS)
                        fxbuffer->p[i].set_storage_value((float)d);
                }
                else
                {
                    if (e->QueryIntAttribute(lbl, &j) == TIXML_SUCCESS)
                        fxbuffer->p[i].set_storage_value(j);
                }

                snprintf(sublbl, TXT_SIZE, "p%i_temposync", i);
                fxbuffer->p[i].temposync =
                    ((e->QueryIntAttribute(sublbl, &j) == TIXML_SUCCESS) && (j == 1));
                snprintf(sublbl, TXT_SIZE, "p%i_extend_range", i);
                fxbuffer->p[i].set_extend_range(
                    ((e->QueryIntAttribute(sublbl, &j) == TIXML_SUCCESS) && (j == 1)));
                snprintf(sublbl, TXT_SIZE, "p%i_deactivated", i);
                fxbuffer->p[i].deactivated =
                    ((e->QueryIntAttribute(sublbl, &j) == TIXML_SUCCESS) && (j == 1));
            }
        }
        else
        {
            selectedName = "Off";
        }
    }
}

void FxMenu::populate()
{
    auto sge = firstListenerOfType<SurgeGUIEditor>();

    // Are there any user presets?
    storage->fxUserPreset->doPresetRescan(storage);

    XMLMenuPopulator::populate();

    menu.addColumnBreak();
    menu.addSectionHeader("FUNCTIONS");

    menu.addItem(Surge::GUI::toOSCaseForMenu("Clear Current FX Unit"), [this]() {
        loadSnapshot(fxt_off, nullptr, 0);
        if (getControlListener())
        {
            getControlListener()->valueChanged(asControlValueInterface());
        }
        repaint();
    });

    if (sge)
    {
        auto initSubmenu = juce::PopupMenu();

        initSubmenu.addItem(Surge::GUI::toOSCaseForMenu("Clear Scene A Insert FX Chain"), true,
                            false, [this, sge]() { sge->enqueueFXChainClear(0); });

        initSubmenu.addItem(Surge::GUI::toOSCaseForMenu("Clear Scene B Insert FX Chain"), true,
                            false, [this, sge]() { sge->enqueueFXChainClear(1); });

        initSubmenu.addItem(Surge::GUI::toOSCaseForMenu("Clear Send FX Chain"), true, false,
                            [this, sge]() { sge->enqueueFXChainClear(2); });

        initSubmenu.addItem(Surge::GUI::toOSCaseForMenu("Clear Global FX Chain"), true, false,
                            [this, sge]() { sge->enqueueFXChainClear(3); });

        initSubmenu.addItem(Surge::GUI::toOSCaseForMenu("Clear All FX Chains"), true, false,
                            [this, sge]() { sge->enqueueFXChainClear(-1); });

        menu.addSubMenu(Surge::GUI::toOSCaseForMenu("Clear Chains"), initSubmenu);
    }

    menu.addSeparator();

    auto rsA = [this, sge]() {
        this->storage->fxUserPreset->doPresetRescan(this->storage, true);

        if (sge)
        {
            sge->queueRebuildUI();
        }
    };

    menu.addItem(Surge::GUI::toOSCaseForMenu("Refresh FX Preset List"), rsA);
    if (fx->type.val.i != fxt_off)
    {
        menu.addItem(Surge::GUI::toOSCaseForMenu("Save FX Preset As..."),
                     [this]() { this->saveFX(); });
    }

    menu.addSeparator();

    menu.addItem(Surge::GUI::toOSCaseForMenu("Copy FX Preset"), [this]() { this->copyFX(); });
    if (Surge::FxClipboard::isPasteAvailable(fxClipboard))
    {
        menu.addItem(Surge::GUI::toOSCaseForMenu("Paste FX Preset"), [this]() { this->pasteFX(); });
    }

    menu.addSeparator();

    if (sge)
    {
        auto hu = sge->helpURLForSpecial("fx-presets");
        auto lurl = hu;

        if (hu != "")
        {
            lurl = sge->fullyResolvedHelpURL(hu);
        }

        auto hmen = std::make_unique<Surge::Widgets::MenuTitleHelpComponent>("FX Presets", lurl);
        hmen->setSkin(skin, associatedBitmapStore);
        hmen->setCenterBold(false);

        menu.addCustomItem(-1, std::move(hmen));
    }
}

Surge::FxClipboard::Clipboard FxMenu::fxClipboard;

void FxMenu::copyFX()
{
    Surge::FxClipboard::copyFx(storage, fx, fxClipboard);
    memcpy((void *)fxbuffer, (void *)fx, sizeof(FxStorage));
}

void FxMenu::pasteFX()
{
    Surge::FxClipboard::pasteFx(storage, fxbuffer, fxClipboard);

    selectedName = std::string("Copied ") + fx_type_shortnames[fxbuffer->type.val.i];

    notifyValueChanged();
}

void FxMenu::saveFX()
{
    auto *sge = firstListenerOfType<SurgeGUIEditor>();
    if (sge)
    {
        sge->promptForMiniEdit("", "Enter the preset name:", "Save FX Preset", juce::Point<int>{},
                               [this](const std::string &s) {
                                   this->storage->fxUserPreset->saveFxIn(this->storage, fx, s);
                                   auto *sge = firstListenerOfType<SurgeGUIEditor>();
                                   if (sge)
                                       sge->queueRebuildUI();
                               });
    }
}

void FxMenu::loadUserPreset(const Surge::Storage::FxUserPreset::Preset &p)
{
    this->storage->fxUserPreset->loadPresetOnto(p, storage, fxbuffer);

    selectedIdx = -1;
    selectedName = p.name;

    notifyValueChanged();
}

void FxMenu::scanExtraPresets()
{
    storage->fxUserPreset->doPresetRescan(storage);
    for (const auto &tp : storage->fxUserPreset->getPresetsByType())
    {
        // So let's run all presets until we find the first item with type tp.first
        auto alit = allPresets.begin();
        while (alit->itemType != tp.first && alit != allPresets.end())
            alit++;
        std::vector<std::string> rootPath;
        rootPath.push_back(alit->pathElements[0]);

        while (alit != allPresets.end() && alit->itemType == tp.first)
            alit++;

        // OK so alit now points at the end of the list
        alit = allPresets.insert(alit, tp.second.size(), Item());

        // OK so insert 'size' into all presets at the end of the position of the type

        for (const auto ps : tp.second)
        {
            alit->itemType = tp.first;
            alit->name = ps.name;
            alit->isUser = !ps.isFactory;
            alit->path = string_to_path(ps.file);
            auto thisPath = rootPath;
            for (const auto &q : ps.subPath)
                thisPath.push_back(path_to_string(q));

            alit->pathElements = thisPath;
            alit->hasFxUserPreset = true;
            alit->fxPreset = ps;
            alit++;
        }
    }
}

void FxMenu::loadByIndex(int index)
{
    auto q = allPresets[index];
    if (q.xmlElement)
    {
        loadSnapshot(q.itemType, q.xmlElement, index);
    }
    else
    {
        loadUserPreset(q.fxPreset);
    }
    selectedIdx = index;
    if (getControlListener())
        getControlListener()->valueChanged(asControlValueInterface());
    repaint();
}

void FxMenu::mouseWheelMove(const juce::MouseEvent &event, const juce::MouseWheelDetails &wheel)
{
    int dir = wheelAccumulationHelper.accumulate(wheel, false, true);

    if (dir != 0)
    {
        jogBy(-dir);
    }
}

bool FxMenu::keyPressed(const juce::KeyPress &key)
{
    auto [action, mod] = Surge::Widgets::accessibleEditAction(key, storage);

    if (action == None)
        return false;

    if (action == OpenMenu || action == Return)
    {
        menu.showMenuAsync(juce::PopupMenu::Options());
        return true;
    }

    switch (action)
    {
    case Increase:
        jogBy(+1);
        return true;
        break;
    case Decrease:
        jogBy(-1);
        return true;
        break;
    default:
        return false; // but bail out if it does
        break;
    }

    return false;
}

template <> struct XMLValue<FxMenu>
{
    static std::string value(FxMenu *comp)
    {
        std::string r = fx_type_shortnames[comp->fx->type.val.i];
        r += " in ";
        r += fxslot_names[comp->current_fx];
        return r;
    }
};

std::unique_ptr<juce::AccessibilityHandler> FxMenu::createAccessibilityHandler()
{
    return std::make_unique<XMLMenuAH<FxMenu>>(this);
}

} // namespace Widgets
} // namespace Surge
