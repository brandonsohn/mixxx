#include <QMessageBox>

#include "library/autodj/dlgautodj.h"

#include "library/playlisttablemodel.h"
#include "util/assert.h"
#include "util/compatibility.h"
#include "util/duration.h"
#include "widget/wtracktableview.h"

namespace {
const char* kPreferenceGroupName = "[Auto DJ]";
const char* kRepeatPlaylistPreference = "Requeue";
const char* kEnableButtonName = "AutoDjEnable";
const char* kShuffleButtonName = "AutoDjShuffle";
const char* kSkipButtonName = "AutoDjSkip";
const char* kAddRandomButtonName = "AutoDjAddRandom";
const char* kFadeNowButtonName = "AutoDjFadeNow";
const char* kRepeatButtonName = "AutoDjRepeatPlaylist";
} // anonymous namespace

DlgAutoDJ::DlgAutoDJ(QWidget* parent,
                     UserSettingsPointer pConfig,
                     Library* pLibrary,
                     AutoDJProcessor* pProcessor,
                     TrackCollection* pTrackCollection,
                     KeyboardEventFilter* pKeyboard,
                     QMap<QString, SkinButton> icons)
        : QWidget(parent),
          Ui::DlgAutoDJ(),
          m_pAutoDJProcessor(pProcessor),
          // no sorting
          m_pTrackTableView(new WTrackTableView(this, pConfig,
                                                pTrackCollection, false)),
          m_pAutoDJTableModel(nullptr),
          m_pConfig(pConfig),
          m_icons(icons) {
    setupUi(this);

    m_pTrackTableView->installEventFilter(pKeyboard);
    connect(m_pTrackTableView, &WTrackTableView::loadTrack,
            this, &DlgAutoDJ::loadTrack);
    connect(m_pTrackTableView, &WTrackTableView::loadTrackToPlayer,
            this, &DlgAutoDJ::loadTrackToPlayer);
    connect(m_pTrackTableView, &WTrackTableView::trackSelected,
            this, &DlgAutoDJ::trackSelected);
    connect(m_pTrackTableView, &WTrackTableView::trackSelected,
            this, &DlgAutoDJ::updateSelectionInfo);

    connect(pLibrary, &Library::setTrackTableFont,
            m_pTrackTableView, &WTrackTableView::setTrackTableFont);
    connect(pLibrary, &Library::setTrackTableRowHeight,
            m_pTrackTableView, &WTrackTableView::setTrackTableRowHeight);
    connect(pLibrary, &Library::setSelectedClick,
            m_pTrackTableView, &WTrackTableView::setSelectedClick);

    QBoxLayout* box = dynamic_cast<QBoxLayout*>(layout());
    VERIFY_OR_DEBUG_ASSERT(box) { //Assumes the form layout is a QVBox/QHBoxLayout!
    } else {
        box->removeWidget(m_pTrackTablePlaceholder);
        m_pTrackTablePlaceholder->hide();
        box->insertWidget(1, m_pTrackTableView);
    }

    // We do _NOT_ take ownership of this from AutoDJProcessor.
    m_pAutoDJTableModel = m_pAutoDJProcessor->getTableModel();
    m_pTrackTableView->loadTrackModel(m_pAutoDJTableModel);

    // Override some playlist-view properties:

    // Do not set this because it disables auto-scrolling
    //m_pTrackTableView->setDragDropMode(QAbstractItemView::InternalMove);

    connect(pushButtonAutoDJ, &QPushButton::toggled,
            this, &DlgAutoDJ::toggleAutoDJButton);

    setupActionButton(pushButtonShuffle, &DlgAutoDJ::shufflePlaylistButton,
                      kShuffleButtonName, tr("Shuffle"));
    setupActionButton(pushButtonSkipNext, &DlgAutoDJ::skipNextButton,
                      kSkipButtonName, tr("Skip"));
    setupActionButton(pushButtonAddRandom, &DlgAutoDJ::addRandomButton,
                      kAddRandomButtonName, tr("Random"));
    setupActionButton(pushButtonFadeNow, &DlgAutoDJ::fadeNowButton,
                      kFadeNowButtonName, tr("Fade"));

    connect(spinBoxTransition, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &DlgAutoDJ::transitionSliderChanged);

    fadeModeCombobox->addItem(tr("Full Intro + Outro"),
                                static_cast<int>(AutoDJProcessor::TransitionMode::FullIntroOutro));
    fadeModeCombobox->addItem(tr("Fade At Outro Start"),
                                static_cast<int>(AutoDJProcessor::TransitionMode::FadeAtOutroStart));
    fadeModeCombobox->addItem(tr("Full Track"),
                                static_cast<int>(AutoDJProcessor::TransitionMode::FixedFullTrack));
    fadeModeCombobox->addItem(tr("Skip Silence"),
                                static_cast<int>(AutoDJProcessor::TransitionMode::FixedSkipSilence));
    fadeModeCombobox->setCurrentIndex(
            fadeModeCombobox->findData(static_cast<int>(m_pAutoDJProcessor->getTransitionMode())));
    connect(fadeModeCombobox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DlgAutoDJ::slotTransitionModeChanged);
    QString fadeModeTooltip = tr(
    "Full Intro + Outro:\n"
    "Play the full intro and outro. Use the intro or outro length as the\n"
    "crossfade time, whichever is shorter. If no intro or outro are marked,\n"
    "use the selected crossfade time.\n"
    "\n"
    "Fade At Outro Start:\n"
    "Start crossfading at the outro start. If the outro is longer than the\n"
    "intro, cut off the end of the outro. Use the intro or outro length as\n"
    "the crossfade time, whichever is shorter. If no intro or outro are\n"
    "marked, use the selected crossfade time.\n"
    "\n"
    "Full Track:\n"
    "Play the whole track. Begin crossfading from the selected number of\n"
    "seconds before the end of the track. A negative crossfade time adds\n"
    "silence between tracks.\n"
    "\n"
    "Skip Silence:\n"
    "Play the whole track except for silence at the beginning and end.\n"
    "Begin crossfading from the selected number of seconds before the\n"
    "last sound."
    );
    fadeModeCombobox->setToolTip(fadeModeTooltip);

    connect(pushButtonRepeatPlaylist, &QPushButton::toggled,
            this, &DlgAutoDJ::slotRepeatPlaylistChanged);
    bool repeatPlaylist = m_pConfig->getValue<bool>(
            ConfigKey(kPreferenceGroupName, kRepeatPlaylistPreference));
    pushButtonRepeatPlaylist->setChecked(repeatPlaylist);
    slotRepeatPlaylistChanged(repeatPlaylist);

    // Setup DlgAutoDJ UI based on the current AutoDJProcessor state. Keep in
    // mind that AutoDJ may already be active when DlgAutoDJ is created (due to
    // skin changes, etc.).
    spinBoxTransition->setValue(m_pAutoDJProcessor->getTransitionTime());
    connect(m_pAutoDJProcessor, &AutoDJProcessor::transitionTimeChanged,
            this, &DlgAutoDJ::transitionTimeChanged);
    connect(m_pAutoDJProcessor, &AutoDJProcessor::autoDJStateChanged,
            this, &DlgAutoDJ::autoDJStateChanged);
    autoDJStateChanged(m_pAutoDJProcessor->getState());

    updateSelectionInfo();
}

DlgAutoDJ::~DlgAutoDJ() {
    qDebug() << "~DlgAutoDJ()";

    // Delete m_pTrackTableView before the table model. This is because the
    // table view saves the header state using the model.
    delete m_pTrackTableView;
}

void DlgAutoDJ::setupActionButton(QPushButton* pButton, void (DlgAutoDJ::*pSlot)(bool),
                           QString skinButtonName, QString fallbackText) {
    connect(pButton, &QPushButton::clicked, this, pSlot);
    QString iconPath = m_icons.value(skinButtonName).states.value("Off").pixmapSource.getPath();
    if (iconPath.isEmpty()) {
        pButton->setText(fallbackText);
    } else {
        pButton->setIcon(QIcon(iconPath));
    }
}

void DlgAutoDJ::onShow() {
    m_pAutoDJTableModel->select();
}

void DlgAutoDJ::onSearch(const QString& text) {
    // Do not allow filtering the Auto DJ playlist, because
    // Auto DJ will work from the filtered table
    Q_UNUSED(text);
}

void DlgAutoDJ::loadSelectedTrack() {
    m_pTrackTableView->loadSelectedTrack();
}

void DlgAutoDJ::loadSelectedTrackToGroup(QString group, bool play) {
    m_pTrackTableView->loadSelectedTrackToGroup(group, play);
}

void DlgAutoDJ::moveSelection(int delta) {
    m_pTrackTableView->moveSelection(delta);
}

void DlgAutoDJ::shufflePlaylistButton(bool) {
    QModelIndexList indexList = m_pTrackTableView->selectionModel()->selectedRows();

    // Activate regardless of button being checked
    m_pAutoDJProcessor->shufflePlaylist(indexList);
}

void DlgAutoDJ::skipNextButton(bool) {
    // Activate regardless of button being checked
    m_pAutoDJProcessor->skipNext();
}

void DlgAutoDJ::fadeNowButton(bool) {
    // Activate regardless of button being checked
    m_pAutoDJProcessor->fadeNow();
}

void DlgAutoDJ::toggleAutoDJButton(bool enable) {
    AutoDJProcessor::AutoDJError error = m_pAutoDJProcessor->toggleAutoDJ(enable);
    switch (error) {
        case AutoDJProcessor::ADJ_BOTH_DECKS_PLAYING:
            QMessageBox::warning(
                    NULL, tr("Auto DJ"),
                    tr("One deck must be stopped to enable Auto DJ mode."),
                    QMessageBox::Ok);
            // Make sure the button becomes unpushed.
            pushButtonAutoDJ->setChecked(false);
            break;
        case AutoDJProcessor::ADJ_DECKS_3_4_PLAYING:
            QMessageBox::warning(
                    NULL, tr("Auto DJ"),
                    tr("Decks 3 and 4 must be stopped to enable Auto DJ mode."),
                    QMessageBox::Ok);
            pushButtonAutoDJ->setChecked(false);
            break;
        case AutoDJProcessor::ADJ_OK:
        default:
            break;
    }
}

void DlgAutoDJ::transitionTimeChanged(int time) {
    spinBoxTransition->setValue(time);
}

void DlgAutoDJ::transitionSliderChanged(int value) {
    m_pAutoDJProcessor->setTransitionTime(value);
}

void DlgAutoDJ::autoDJStateChanged(AutoDJProcessor::AutoDJState state) {
    QString stateName;
    if (state == AutoDJProcessor::ADJ_DISABLED) {
        pushButtonAutoDJ->setChecked(false);
        pushButtonAutoDJ->setToolTip(tr("Enable Auto DJ"));
        stateName = "Off";
        pushButtonFadeNow->setEnabled(false);
        pushButtonSkipNext->setEnabled(false);
    } else {
        // No matter the mode, you can always disable once it is enabled.
        pushButtonAutoDJ->setChecked(true);
        pushButtonAutoDJ->setToolTip(tr("Disable Auto DJ"));
        stateName = "On";

        // If fading, you can't hit fade now.
        if (state == AutoDJProcessor::ADJ_LEFT_FADING ||
                state == AutoDJProcessor::ADJ_RIGHT_FADING ||
                state == AutoDJProcessor::ADJ_ENABLE_P1LOADED) {
            pushButtonFadeNow->setEnabled(false);
        } else {
            pushButtonFadeNow->setEnabled(true);
        }

        // You can always skip the next track if we are enabled.
        pushButtonSkipNext->setEnabled(true);
    }
    QString pixmapPath = m_icons.value(kEnableButtonName).states.value(stateName).pixmapSource.getPath();
    if (pixmapPath.isEmpty()) {
        pushButtonAutoDJ->setText((state == AutoDJProcessor::ADJ_DISABLED) ? tr("Enable") : tr("Disable"));
    } else {
        pushButtonAutoDJ->setIcon(QIcon(pixmapPath));
    }
}

void DlgAutoDJ::slotTransitionModeChanged(int comboboxIndex) {
    m_pAutoDJProcessor->setTransitionMode(static_cast<AutoDJProcessor::TransitionMode>(
          fadeModeCombobox->itemData(comboboxIndex).toInt()));
}

void DlgAutoDJ::slotRepeatPlaylistChanged(int checkState) {
    bool checked = static_cast<bool>(checkState);
    m_pConfig->setValue(ConfigKey(kPreferenceGroupName, kRepeatPlaylistPreference),
            checked);
    QString stateName = checked ? "On" : "Off";
    QString pixmapPath = m_icons.value(kRepeatButtonName).states.value(stateName).pixmapSource.getPath();
    if (pixmapPath.isEmpty()) {
        pushButtonRepeatPlaylist->setText(tr("Repeat"));
    } else {
        pushButtonRepeatPlaylist->setIcon(QIcon(pixmapPath));
    }
}

void DlgAutoDJ::updateSelectionInfo() {
    double duration = 0.0;

    QModelIndexList indices = m_pTrackTableView->selectionModel()->selectedRows();

    for (int i = 0; i < indices.size(); ++i) {
        TrackPointer pTrack = m_pAutoDJTableModel->getTrack(indices.at(i));
        if (pTrack) {
            duration += pTrack->getDuration();
        }
    }

    QString label;

    if (!indices.isEmpty()) {
        label.append(mixxx::DurationBase::formatTime(duration));
        label.append(QString(" (%1)").arg(indices.size()));
        labelSelectionInfo->setToolTip(tr("Displays the duration and number of selected tracks."));
        labelSelectionInfo->setText(label);
        labelSelectionInfo->setEnabled(true);
    } else {
        labelSelectionInfo->setText("");
        labelSelectionInfo->setEnabled(false);
    }
}

bool DlgAutoDJ::hasFocus() const {
    return QWidget::hasFocus();
}
