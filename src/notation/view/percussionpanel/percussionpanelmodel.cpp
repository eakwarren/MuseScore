/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2024 MuseScore Limited
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "percussionpanelmodel.h"

#include "notation/utilities/percussionutilities.h"

#include "types/translatablestring.h"
#include "ui/view/iconcodes.h"

#include "engraving/dom/factory.h"
#include "engraving/dom/undo.h"
#include "engraving/dom/utils.h"

#include "audio/audioutils.h"

static const QString PAD_NAMES_CODE("percussion-pad-names");
static const QString NOTATION_PREVIEW_CODE("percussion-notation-preview");
static const QString EDIT_LAYOUT_CODE("percussion-edit-layout");
static const QString RESET_LAYOUT_CODE("percussion-reset-layout");

using namespace muse;
using namespace ui;
using namespace mu::notation;

PercussionPanelModel::PercussionPanelModel(QObject* parent)
    : QObject(parent)
{
    m_padListModel = new PercussionPanelPadListModel(this);
    qApp->installEventFilter(this);
}

bool PercussionPanelModel::enabled() const
{
    return m_enabled;
}

void PercussionPanelModel::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    emit enabledChanged();
}

QString PercussionPanelModel::soundTitle() const
{
    return m_soundTitle;
}

void PercussionPanelModel::setSoundTitle(const QString& soundTitle)
{
    if (m_soundTitle == soundTitle) {
        return;
    }
    m_soundTitle = soundTitle;
    emit soundTitleChanged();
}

PanelMode::Mode PercussionPanelModel::currentPanelMode() const
{
    return m_currentPanelMode;
}

void PercussionPanelModel::setCurrentPanelMode(const PanelMode::Mode& panelMode)
{
    if (m_currentPanelMode == panelMode) {
        return;
    }

    // After editing, return to the last non-edit mode...
    if (panelMode != PanelMode::Mode::EDIT_LAYOUT && m_panelModeToRestore != panelMode) {
        m_panelModeToRestore = panelMode;
    }

    m_currentPanelMode = panelMode;
    emit currentPanelModeChanged(m_currentPanelMode);
}

bool PercussionPanelModel::useNotationPreview() const
{
    return m_useNotationPreview;
}

void PercussionPanelModel::setUseNotationPreview(bool useNotationPreview)
{
    if (m_useNotationPreview == useNotationPreview) {
        return;
    }

    m_useNotationPreview = useNotationPreview;
    emit useNotationPreviewChanged(m_useNotationPreview);
}

PercussionPanelPadListModel* PercussionPanelModel::padListModel() const
{
    return m_padListModel;
}

void PercussionPanelModel::init()
{
    setUpConnections();

    globalContext()->currentNotationChanged().onNotify(this, [this] {
        setUpConnections();
    });
}

QList<QVariantMap> PercussionPanelModel::layoutMenuItems() const
{
    const auto editLayoutTitle = m_currentPanelMode == PanelMode::Mode::EDIT_LAYOUT
                                 ? muse::qtrc("notation/percussion", "Finish editing")
                                 : muse::qtrc("notation/percussion", "Edit layout");

    static constexpr int editLayoutIcon = static_cast<int>(IconCode::Code::CONFIGURE);
    static constexpr int resetLayoutIcon = static_cast<int>(IconCode::Code::UNDO);

    QList<QVariantMap> menuItems = {
        { { "id", PAD_NAMES_CODE }, { "title", muse::qtrc("notation/percussion", "Pad names") },
            { "checkable", true }, { "checked", !m_useNotationPreview }, { "enabled", true } },

        { { "id", NOTATION_PREVIEW_CODE }, { "title", muse::qtrc("notation/percussion", "Notation preview") },
            { "checkable", true }, { "checked", m_useNotationPreview }, { "enabled", true } },

        { }, // separator

        { { "id", EDIT_LAYOUT_CODE },
            { "title", editLayoutTitle }, { "icon", editLayoutIcon }, { "enabled", true } },

        { { "id", RESET_LAYOUT_CODE },
            { "title", muse::qtrc("notation/percussion", "Reset layout") }, { "icon", resetLayoutIcon }, { "enabled", true } },
    };

    return menuItems;
}

void PercussionPanelModel::handleMenuItem(const QString& itemId)
{
    if (itemId == PAD_NAMES_CODE) {
        setUseNotationPreview(false);
    } else if (itemId == NOTATION_PREVIEW_CODE) {
        setUseNotationPreview(true);
    } else if (itemId == EDIT_LAYOUT_CODE) {
        if (m_currentPanelMode == PanelMode::Mode::EDIT_LAYOUT) {
            finishEditing();
            m_padListModel->focusFirstActivePad();
            return;
        }
        setCurrentPanelMode(PanelMode::Mode::EDIT_LAYOUT);
        m_padListModel->padFocusRequested(0);
    } else if (itemId == RESET_LAYOUT_CODE) {
        resetLayout();
    }
}

void PercussionPanelModel::finishEditing(bool discardChanges)
{
    if (!interaction()) {
        //! NOTE: Can happen if we close the project while editing the layout...
        m_padListModel->setDrumset(nullptr);
        setCurrentPanelMode(m_panelModeToRestore);
        return;
    }

    m_padListModel->removeEmptyRows();

    const std::pair<Instrument*, Part*> instAndPart = getCurrentInstrumentAndPart();
    Instrument* inst = instAndPart.first;
    Part* part = instAndPart.second;

    if (discardChanges) {
        m_padListModel->setDrumset(inst ? inst->drumset() : nullptr);
        setCurrentPanelMode(m_panelModeToRestore);
        return;
    }

    IF_ASSERT_FAILED(inst && inst->drumset() && part) {
        return;
    }

    Drumset updatedDrumset = *m_padListModel->drumset();

    for (int i = 0; i < m_padListModel->padList().size(); ++i) {
        const PercussionPanelPadModel* model = m_padListModel->padList().at(i);
        if (!model) {
            continue;
        }

        const int row = i / m_padListModel->numColumns();
        const int column = i % m_padListModel->numColumns();

        engraving::DrumInstrument& drum = updatedDrumset.drum(model->pitch());

        drum.panelRow = row;
        drum.panelColumn = column;

        const QString& shortcut = model->keyboardShortcut();
        drum.shortcut = shortcut.isEmpty() ? '\0' : shortcut.toLatin1().at(0);
    }

    // Return if nothing changed after edit...
    if (*inst->drumset() == updatedDrumset) {
        setCurrentPanelMode(m_panelModeToRestore);
        m_padListModel->focusLastActivePad();
        return;
    }

    INotationUndoStackPtr undoStack = notation()->undoStack();

    undoStack->prepareChanges(muse::TranslatableString("undoableAction", "Edit percussion panel layout"));
    score()->undo(new engraving::ChangeDrumset(inst, updatedDrumset, part));
    undoStack->commitChanges();

    setCurrentPanelMode(m_panelModeToRestore);
    m_padListModel->focusLastActivePad();
}

void PercussionPanelModel::customizeKit()
{
    dispatcher()->dispatch("customize-kit");
}

void PercussionPanelModel::setUpConnections()
{
    const auto updatePadModels = [this](Drumset* drumset) {
        if (drumset && m_padListModel->drumset() && *drumset == *m_padListModel->drumset()) {
            return;
        }

        if (m_currentPanelMode == PanelMode::Mode::EDIT_LAYOUT) {
            finishEditing(/*discardChanges*/ true);
        }

        m_padListModel->setDrumset(drumset);
        updateSoundTitle(currentTrackId());
    };

    if (!notation()) {
        updatePadModels(nullptr);
        return;
    }

    const INotationNoteInputPtr noteInput = interaction()->noteInput();
    updatePadModels(noteInput->state().drumset);
    setEnabled(m_padListModel->hasActivePads());

    noteInput->stateChanged().onNotify(this, [this, updatePadModels]() {
        if (!notation()) {
            updatePadModels(nullptr);
            return;
        }
        const INotationNoteInputPtr ni = interaction()->noteInput();
        updatePadModels(ni->state().drumset);
    });

    m_padListModel->hasActivePadsChanged().onNotify(this, [this]() {
        setEnabled(m_padListModel->hasActivePads());
    });

    m_padListModel->padActionRequested().onReceive(this, [this](PercussionPanelPadModel::PadAction action, int pitch) {
        switch (action) {
        case PercussionPanelPadModel::PadAction::TRIGGER:
            onPadTriggered(pitch);
            break;
        case PercussionPanelPadModel::PadAction::DUPLICATE:
            onDuplicatePadRequested(pitch);
            break;
        case PercussionPanelPadModel::PadAction::DELETE:
            onDeletePadRequested(pitch);
            break;
        case PercussionPanelPadModel::PadAction::DEFINE_SHORTCUT:
            onDefinePadShortcutRequested(pitch);
            break;
        default:
            break;
        }
    });

    if (!audioSettings()) {
        return;
    }

    audioSettings()->trackInputParamsChanged().onReceive(this, [this](InstrumentTrackId trackId) {
        if (trackId != currentTrackId()) {
            return;
        }
        updateSoundTitle(trackId);
    });
}

void PercussionPanelModel::updateSoundTitle(const InstrumentTrackId& trackId)
{
    if (!trackId.isValid()) {
        setSoundTitle(QString());
        return;
    }

    const audio::AudioInputParams& params = audioSettings()->trackInputParams(trackId);

    const QString name = muse::audio::audioSourceName(params).toQString();
    const QString category = muse::audio::audioSourceCategoryName(params).toQString();
    if (name.isEmpty() || category.isEmpty()) {
        setSoundTitle(QString());
        return;
    }

    setSoundTitle(category + ": " + name);
}

bool PercussionPanelModel::eventFilter(QObject* watched, QEvent* event)
{
    // Finish editing on escape...
    if (m_currentPanelMode != PanelMode::Mode::EDIT_LAYOUT || event->type() != QEvent::Type::ShortcutOverride
        || m_padListModel->swapInProgress()) {
        return QObject::eventFilter(watched, event);
    }
    QKeyEvent* keyEvent = dynamic_cast<QKeyEvent*>(event);
    if (!keyEvent || keyEvent->key() != Qt::Key_Escape) {
        return QObject::eventFilter(watched, event);
    }
    finishEditing();
    event->setAccepted(true);
    return true;
}

void PercussionPanelModel::onPadTriggered(int pitch)
{
    switch (currentPanelMode()) {
    case PanelMode::Mode::EDIT_LAYOUT: return;
    case PanelMode::Mode::WRITE: writePitch(pitch); // fall through
    case PanelMode::Mode::SOUND_PREVIEW: playPitch(pitch);
    default: break;
    }
}

void PercussionPanelModel::onDuplicatePadRequested(int pitch)
{
    const std::pair<Instrument*, Part*> instAndPart = getCurrentInstrumentAndPart();
    Instrument* inst = instAndPart.first;
    Part* part = instAndPart.second;

    IF_ASSERT_FAILED(inst && part) {
        return;
    }

    const int nextAvailablePitch = m_padListModel->nextAvailablePitch(pitch);
    if (nextAvailablePitch < 0) {
        // TODO: Show some sort of warning dialog?
        LOGE() << "No space to duplicate drum pad";
        return;
    }
    const int nextAvailableIndex = m_padListModel->nextAvailableIndex(pitch);

    Drumset updatedDrumset = *m_padListModel->drumset();

    engraving::DrumInstrument duplicateDrum = updatedDrumset.drum(pitch);

    duplicateDrum.shortcut = '\0'; // Don't steal the shortcut
    duplicateDrum.panelRow = nextAvailableIndex / m_padListModel->numColumns();
    duplicateDrum.panelColumn = nextAvailableIndex % m_padListModel->numColumns();

    updatedDrumset.setDrum(nextAvailablePitch, duplicateDrum);

    INotationUndoStackPtr undoStack = notation()->undoStack();

    undoStack->prepareChanges(muse::TranslatableString("undoableAction", "Duplicate percussion panel pad"));
    score()->undo(new engraving::ChangeDrumset(inst, updatedDrumset, part));
    undoStack->commitChanges();
}

void PercussionPanelModel::onDeletePadRequested(int pitch)
{
    const std::pair<Instrument*, Part*> instAndPart = getCurrentInstrumentAndPart();
    Instrument* inst = instAndPart.first;
    Part* part = instAndPart.second;

    IF_ASSERT_FAILED(inst && part) {
        return;
    }

    Drumset updatedDrumset = *m_padListModel->drumset();
    updatedDrumset.setDrum(pitch, engraving::DrumInstrument());

    INotationUndoStackPtr undoStack = notation()->undoStack();

    undoStack->prepareChanges(muse::TranslatableString("undoableAction", "Delete percussion panel pad"));
    score()->undo(new engraving::ChangeDrumset(inst, updatedDrumset, part));
    undoStack->commitChanges();
}

void PercussionPanelModel::onDefinePadShortcutRequested(int)
{
    // TODO: Design in progress...
}

void PercussionPanelModel::writePitch(int pitch)
{
    INotationUndoStackPtr undoStack = notation()->undoStack();
    if (!interaction() || !undoStack) {
        return;
    }

    undoStack->prepareChanges(muse::TranslatableString("undoableAction", "Enter percussion note"));

    interaction()->noteInput()->startNoteInput(/*focusNotation*/ false);

    score()->addMidiPitch(pitch, false, /*transpose*/ false);
    undoStack->commitChanges();

    const mu::engraving::InputState& inputState = score()->inputState();
    if (inputState.cr()) {
        interaction()->showItem(inputState.cr());
    }
}

void PercussionPanelModel::playPitch(int pitch)
{
    if (!interaction()) {
        return;
    }

    const NoteInputState inputState = interaction()->noteInput()->state();
    std::shared_ptr<Chord> chord = PercussionUtilities::getDrumNoteForPreview(m_padListModel->drumset(), pitch);

    chord->setParent(inputState.segment);
    chord->setTrack(inputState.currentTrack);

    playbackController()->playElements({ chord.get() });
}

void PercussionPanelModel::resetLayout()
{
    if (m_currentPanelMode == PanelMode::Mode::EDIT_LAYOUT) {
        finishEditing(/*discardChanges*/ true);
    }

    const std::pair<Instrument*, Part*> instAndPart = getCurrentInstrumentAndPart();
    Instrument* inst = instAndPart.first;
    Part* part = instAndPart.second;

    IF_ASSERT_FAILED(inst && inst->drumset() && part) {
        return;
    }

    const InstrumentTemplate& instTemplate = instrumentsRepository()->instrumentTemplate(inst->id());
    const Drumset* defaultDrumset = instTemplate.drumset;

    IF_ASSERT_FAILED(defaultDrumset) {
        return;
    }

    Drumset defaultLayout = m_padListModel->constructDefaultLayout(defaultDrumset);
    if (defaultLayout == *m_padListModel->drumset()) {
        // Nothing changed after reset...
        return;
    }

    INotationUndoStackPtr undoStack = notation()->undoStack();

    undoStack->prepareChanges(muse::TranslatableString("undoableAction", "Reset percussion panel layout"));
    score()->undo(new engraving::ChangeDrumset(inst, defaultLayout, part));
    undoStack->commitChanges();
}

InstrumentTrackId PercussionPanelModel::currentTrackId() const
{
    if (!interaction()) {
        return InstrumentTrackId();
    }

    const NoteInputState inputState = interaction()->noteInput()->state();
    const Staff* staff = inputState.staff;

    if (!staff || !staff->part() || !inputState.segment) {
        return InstrumentTrackId();
    }

    return { staff->part()->id(), staff->part()->instrumentId(inputState.segment->tick()) };
}

std::pair<mu::engraving::Instrument*, mu::engraving::Part*> PercussionPanelModel::getCurrentInstrumentAndPart() const
{
    if (!interaction()) {
        return { nullptr, nullptr };
    }

    NoteInputState inputState = interaction()->noteInput()->state();

    const Staff* staff = inputState.staff;

    Part* part = staff ? staff->part() : nullptr;

    Instrument* inst = part ? part->instrument(inputState.segment->tick()) : nullptr;

    return { inst, part };
}

const project::IProjectAudioSettingsPtr PercussionPanelModel::audioSettings() const
{
    return globalContext()->currentProject() ? globalContext()->currentProject()->audioSettings() : nullptr;
}

const INotationPtr PercussionPanelModel::notation() const
{
    return globalContext()->currentNotation();
}

const INotationInteractionPtr PercussionPanelModel::interaction() const
{
    return notation() ? notation()->interaction() : nullptr;
}

mu::engraving::Score* PercussionPanelModel::score() const
{
    return notation() ? notation()->elements()->msScore() : nullptr;
}
