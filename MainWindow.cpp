// Pom1 Apple 1 Emulator
// Copyright (C) 2012 John D. Corrado
// Copyright (C) 2000-2026 Verhille Arnaud
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.


#include <iostream>

#include <QtGui>
#include <QApplication>
#include <QWidget>
#include <QLabel>
#include <QDockWidget>
#include <QAction>
#include <QMenuBar>
#include <QToolBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QClipboard>
#include <QTextEdit>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QPushButton>
#include <QGroupBox>
#include <QSpinBox>
#include <QStatusBar>
#include <QInputDialog>
#include <QRegExp>

#include "MainWindow.h"
#include "MemoryViewer.h"
#include "Memory.h"


MainWindow::MainWindow()
{
    setFixedSize(800, 600);

    createActions();
    createMenus();
    createToolBars();
    createDocks();
    createStatusBar();

    createPom1();

    setCentralWidget(screen);
    setWindowIcon(QIcon(":/images/pom1.png"));
}

MainWindow::~MainWindow()
{
    destroyPom1();
}

void MainWindow::createPom1()
{
    cout << "Welcome to POM1 - Apple I Emulator" << endl;
    memory = new Memory();
    cpu = new M6502(memory);
    screen = new Screen();

    // Le memoryViewer sera initialisé dans createDocks()
    // La population se fera après la création des docks
}

void MainWindow::destroyPom1()
{
    delete memory;
    delete cpu;
    delete screen;
}

void MainWindow::createActions()
{
    loadMemoryAction = new QAction(tr("&Load Memory from disk"),this);
    loadMemoryAction->setIcon(QIcon(":/images/loadMemory.png"));
    connect(loadMemoryAction, SIGNAL(triggered()), this, SLOT(loadMemory()));
    saveMemoryAction = new QAction(tr("&Save Memory to disk"),this);
    saveMemoryAction->setIcon(QIcon(":/images/saveMemory.png"));
    connect(saveMemoryAction, SIGNAL(triggered()), this, SLOT(saveMemory()));
    pasteCodeAction = new QAction(tr("&Paste Code"),this);
    pasteCodeAction->setIcon(QIcon(":/images/pasteCode.png"));
    connect(pasteCodeAction, SIGNAL(triggered()), this, SLOT(pasteCode()));
    quitAction = new QAction(tr("&Quit POM1"),this);
    quitAction->setIcon(QIcon(":/images/quit.png"));
    //quitAction->setIcon(QIcon(":/images/quit.png"));
    connect(quitAction, SIGNAL(triggered()), this, SLOT(quit()));

    resetAction = new QAction(tr("Soft &Reset"),this);
    resetAction->setIcon(QIcon(":/images/reset.png"));
    connect(resetAction, SIGNAL(triggered()), this, SLOT(reset()));
    hardResetAction = new QAction(tr("&Hard Reset"),this);
    hardResetAction->setIcon(QIcon(":/images/hardreset.png"));
    connect(hardResetAction, SIGNAL(triggered()), this, SLOT(hardReset()));
    debugCpuAction = new QAction(tr("&Debug Console"),this);
    debugCpuAction->setIcon(QIcon(":/images/debug.png"));
    connect(debugCpuAction, SIGNAL(triggered()), this, SLOT(debugCpu()));

    configScreenAction = new QAction(tr("&Screen Options"),this);
    configScreenAction->setIcon(QIcon(":/images/screen.png"));
    connect(configScreenAction, SIGNAL(triggered()), this, SLOT(configScreen()));
    configMemoryAction = new QAction(tr("&Memory Options"),this);
    configMemoryAction->setIcon(QIcon(":/images/memory.png"));
    connect(configMemoryAction, SIGNAL(triggered()), this, SLOT(configMemory()));

    aboutAction = new QAction(tr("&About POM1"),this);
    aboutAction->setIcon(QIcon(":/images/about.png"));
    connect(aboutAction, SIGNAL(triggered()), this, SLOT(about()));
    aboutQtAction = new QAction(tr("About &Qt"),this);
    aboutQtAction->setIcon(QIcon(":/images/aboutQt.png"));
    connect(aboutQtAction, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
}

void MainWindow::createMenus()
{
    menuBar()->setNativeMenuBar(false); // Put MenuBar in the MainWindow

    fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(loadMemoryAction);
    fileMenu->addAction(saveMemoryAction);
    fileMenu->addSeparator();
    fileMenu->addAction(pasteCodeAction);
    fileMenu->addSeparator();
    fileMenu->addAction(quitAction);

    cpuMenu = menuBar()->addMenu(tr("&Cpu"));
    cpuMenu->addAction(resetAction);
    cpuMenu->addAction(hardResetAction);
    cpuMenu->addSeparator();
    cpuMenu->addAction(debugCpuAction);

    configurationMenu = menuBar()->addMenu(tr("&Configuration"));
    configurationMenu->addAction(configScreenAction);
    configurationMenu->addAction(configMemoryAction);

    helpMenu = menuBar()->addMenu(tr("Help"));
    helpMenu->addAction(aboutAction);
    helpMenu->addAction(aboutQtAction);
}

void MainWindow::createToolBars()
{
    fileToolBar = addToolBar(tr("&File"));
    fileToolBar->addAction(loadMemoryAction);
    fileToolBar->addAction(saveMemoryAction);
    fileToolBar->addAction(pasteCodeAction);
    cpuToolBar = addToolBar(tr("&Cpu"));
    cpuToolBar->addAction(resetAction);
    cpuToolBar->addAction(hardResetAction);
    cpuToolBar->addAction(debugCpuAction);
    configurationToolBar = addToolBar(tr("&Configuration"));
    configurationToolBar->addAction(configScreenAction);
    configurationToolBar->addAction(configMemoryAction);
    helpToolBar = addToolBar(tr("&Help"));
    helpToolBar->addAction(aboutAction);
}

void MainWindow::createDocks()
{
    QDockWidget *dock = new QDockWidget(tr("Memory Viewer"), this);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    memoryViewer = new MemoryViewer(dock);
    dock->setWidget(memoryViewer);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    cpuMenu->addAction(dock->toggleViewAction());
    
    // Initialiser le visualiseur de mémoire avec le contenu actuel
    if (memory) {
        for (int i = 0; i < 64*1024; i++){
           memoryViewer->populateTable(i, memory->memRead(i));
        }
    }
}

void MainWindow::createStatusBar()
{
    statusBar()->showMessage(tr("Ready"));
}

void MainWindow::loadMemory()
{
    QString fileName = QFileDialog::getOpenFileName(this, 
        tr("Charger un fichier mémoire"), 
        "", 
        tr("Fichiers mémoire (*.mem *.bin);;Tous les fichiers (*.*)"));
    
    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            
            // Demander à l'utilisateur l'adresse de chargement
            bool ok;
            int address = QInputDialog::getInt(this, 
                tr("Adresse de chargement"), 
                tr("Adresse de début (hexadécimal):"), 
                0x0300, 0, 0xFFFF, 1, &ok);
            
            if (ok && address + data.size() <= 0x10000) {
                for (int i = 0; i < data.size(); ++i) {
                    memory->memWrite(address + i, (quint8)data[i]);
                }
                
                // Mettre à jour le visualiseur de mémoire
                for (int i = address; i < address + data.size(); ++i) {
                    memoryViewer->populateTable(i, memory->memRead(i));
                }
                
                statusBar()->showMessage(tr("Mémoire chargée: %1 octets à l'adresse 0x%2")
                    .arg(data.size()).arg(address, 4, 16, QChar('0')).toUpper(), 3000);
            } else {
                QMessageBox::warning(this, tr("Erreur"), 
                    tr("Adresse invalide ou fichier trop volumineux"));
            }
        } else {
            QMessageBox::warning(this, tr("Erreur"), 
                tr("Impossible d'ouvrir le fichier: %1").arg(fileName));
        }
    }
}

void MainWindow::saveMemory()
{
    QString fileName = QFileDialog::getSaveFileName(this, 
        tr("Sauvegarder la mémoire"), 
        "", 
        tr("Fichiers mémoire (*.mem *.bin);;Tous les fichiers (*.*)"));
    
    if (!fileName.isEmpty()) {
        // Demander la plage d'adresses à sauvegarder
        bool ok1, ok2;
        int startAddr = QInputDialog::getInt(this, 
            tr("Adresse de début"), 
            tr("Adresse de début (hexadécimal):"), 
            0x0000, 0, 0xFFFF, 1, &ok1);
        
        if (ok1) {
            int endAddr = QInputDialog::getInt(this, 
                tr("Adresse de fin"), 
                tr("Adresse de fin (hexadécimal):"), 
                0xFFFF, startAddr, 0xFFFF, 1, &ok2);
            
            if (ok2) {
                QFile file(fileName);
                if (file.open(QIODevice::WriteOnly)) {
                    QByteArray data;
                    for (int i = startAddr; i <= endAddr; ++i) {
                        data.append((char)memory->memRead(i));
                    }
                    file.write(data);
                    
                    statusBar()->showMessage(tr("Mémoire sauvegardée: %1 octets (0x%2-0x%3)")
                        .arg(data.size())
                        .arg(startAddr, 4, 16, QChar('0'))
                        .arg(endAddr, 4, 16, QChar('0')).toUpper(), 3000);
                } else {
                    QMessageBox::warning(this, tr("Erreur"), 
                        tr("Impossible de créer le fichier: %1").arg(fileName));
                }
            }
        }
    }
}

void MainWindow::pasteCode()
{
    QClipboard *clipboard = QApplication::clipboard();
    QString text = clipboard->text();
    
    if (text.isEmpty()) {
        QMessageBox::information(this, tr("Information"), 
            tr("Le presse-papiers est vide"));
        return;
    }
    
    // Créer une boîte de dialogue pour coller le code
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Coller du code"));
    dialog.setModal(true);
    dialog.resize(500, 400);
    
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    
    QTextEdit *textEdit = new QTextEdit(&dialog);
    textEdit->setPlainText(text);
    layout->addWidget(textEdit);
    
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    
    QLabel *addressLabel = new QLabel(tr("Adresse de destination:"));
    QSpinBox *addressSpinBox = new QSpinBox();
    addressSpinBox->setRange(0, 0xFFFF);
    addressSpinBox->setValue(0x0300);
    addressSpinBox->setDisplayIntegerBase(16);
    addressSpinBox->setPrefix("0x");
    
    QPushButton *pasteButton = new QPushButton(tr("Coller"));
    QPushButton *cancelButton = new QPushButton(tr("Annuler"));
    
    buttonLayout->addWidget(addressLabel);
    buttonLayout->addWidget(addressSpinBox);
    buttonLayout->addStretch();
    buttonLayout->addWidget(pasteButton);
    buttonLayout->addWidget(cancelButton);
    
    layout->addLayout(buttonLayout);
    
    connect(pasteButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    
    if (dialog.exec() == QDialog::Accepted) {
        QString code = textEdit->toPlainText();
        int address = addressSpinBox->value();
        
        // Convertir le texte en octets (supposant du code hexadécimal)
        QStringList lines = code.split('\n');
        int currentAddr = address;
        int bytesWritten = 0;
        
        for (const QString &line : lines) {
            QString cleanLine = line.trimmed().toUpper();
            if (cleanLine.isEmpty() || cleanLine.startsWith(';')) continue;
            
            // Traiter les lignes avec des valeurs hexadécimales
            QStringList hexValues = cleanLine.split(QRegExp("\\s+"));
            for (const QString &hex : hexValues) {
                if (hex.length() == 2 && hex.contains(QRegExp("^[0-9A-F]{2}$"))) {
                    bool ok;
                    quint8 value = hex.toUInt(&ok, 16);
                    if (ok && currentAddr <= 0xFFFF) {
                        memory->memWrite(currentAddr, value);
                        memoryViewer->populateTable(currentAddr, value);
                        currentAddr++;
                        bytesWritten++;
                    }
                }
            }
        }
        
        statusBar()->showMessage(tr("Code collé: %1 octets à partir de 0x%2")
            .arg(bytesWritten).arg(address, 4, 16, QChar('0')).toUpper(), 3000);
    }
}

void MainWindow::quit()
{
    int reponse = QMessageBox::question(this, tr("Quitter"),
        tr("Voulez-vous vraiment quitter POM1 ?"),
        QMessageBox::Yes | QMessageBox::No);
    if (reponse == QMessageBox::Yes) {
        close();
    }
}

void MainWindow::reset()
{
    cpu->softReset();
    statusBar()->showMessage(tr("Reset logiciel effectué"), 2000);
}

void MainWindow::hardReset()
{
    cpu->hardReset();
    memory->resetMemory();
    memory->initMemory();
    
    // Mettre à jour le visualiseur de mémoire
    for (int i = 0; i < 64*1024; i++) {
        memoryViewer->populateTable(i, memory->memRead(i));
    }
    
    statusBar()->showMessage(tr("Reset matériel effectué - Mémoire réinitialisée"), 2000);
}

void MainWindow::debugCpu()
{
    QDialog *debugDialog = new QDialog(this);
    debugDialog->setWindowTitle(tr("Console de débogage CPU"));
    debugDialog->setModal(false);
    debugDialog->resize(600, 400);
    
    QVBoxLayout *layout = new QVBoxLayout(debugDialog);
    
    // Informations sur les registres
    QGroupBox *registersGroup = new QGroupBox(tr("Registres"));
    QVBoxLayout *regLayout = new QVBoxLayout(registersGroup);
    
    QLabel *regInfo = new QLabel(tr("Informations sur les registres du CPU 6502\n"
                                   "Cette fonctionnalité nécessite l'ajout de méthodes\n"
                                   "d'accès aux registres dans la classe M6502."));
    regLayout->addWidget(regInfo);
    
    layout->addWidget(registersGroup);
    
    // Contrôles de débogage
    QGroupBox *controlsGroup = new QGroupBox(tr("Contrôles"));
    QHBoxLayout *controlsLayout = new QHBoxLayout(controlsGroup);
    
    QPushButton *stepButton = new QPushButton(tr("Pas à pas"));
    QPushButton *runButton = new QPushButton(tr("Exécuter"));
    QPushButton *stopButton = new QPushButton(tr("Arrêter"));
    
    controlsLayout->addWidget(stepButton);
    controlsLayout->addWidget(runButton);
    controlsLayout->addWidget(stopButton);
    controlsLayout->addStretch();
    
    layout->addWidget(controlsGroup);
    
    QPushButton *closeButton = new QPushButton(tr("Fermer"));
    connect(closeButton, &QPushButton::clicked, debugDialog, &QDialog::close);
    layout->addWidget(closeButton);
    
    debugDialog->show();
}

void MainWindow::configScreen()
{
    QDialog *configDialog = new QDialog(this);
    configDialog->setWindowTitle(tr("Configuration de l'écran"));
    configDialog->setModal(true);
    configDialog->resize(400, 300);
    
    QVBoxLayout *layout = new QVBoxLayout(configDialog);
    
    QGroupBox *displayGroup = new QGroupBox(tr("Affichage"));
    QVBoxLayout *displayLayout = new QVBoxLayout(displayGroup);
    
    QCheckBox *fullscreenCheck = new QCheckBox(tr("Mode plein écran"));
    QCheckBox *scanLinesCheck = new QCheckBox(tr("Lignes de balayage"));
    QCheckBox *greenMonitorCheck = new QCheckBox(tr("Moniteur vert (style vintage)"));
    
    displayLayout->addWidget(fullscreenCheck);
    displayLayout->addWidget(scanLinesCheck);
    displayLayout->addWidget(greenMonitorCheck);
    
    layout->addWidget(displayGroup);
    
    QGroupBox *sizeGroup = new QGroupBox(tr("Taille"));
    QHBoxLayout *sizeLayout = new QHBoxLayout(sizeGroup);
    
    QLabel *scaleLabel = new QLabel(tr("Échelle:"));
    QSpinBox *scaleSpinBox = new QSpinBox();
    scaleSpinBox->setRange(1, 4);
    scaleSpinBox->setValue(2);
    scaleSpinBox->setSuffix("x");
    
    sizeLayout->addWidget(scaleLabel);
    sizeLayout->addWidget(scaleSpinBox);
    sizeLayout->addStretch();
    
    layout->addWidget(sizeGroup);
    
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *okButton = new QPushButton(tr("OK"));
    QPushButton *cancelButton = new QPushButton(tr("Annuler"));
    
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    
    layout->addLayout(buttonLayout);
    
    connect(okButton, &QPushButton::clicked, configDialog, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, configDialog, &QDialog::reject);
    
    if (configDialog->exec() == QDialog::Accepted) {
        statusBar()->showMessage(tr("Configuration de l'écran mise à jour"), 2000);
    }
}

void MainWindow::configMemory()
{
    QDialog *configDialog = new QDialog(this);
    configDialog->setWindowTitle(tr("Configuration de la mémoire"));
    configDialog->setModal(true);
    configDialog->resize(400, 350);
    
    QVBoxLayout *layout = new QVBoxLayout(configDialog);
    
    QGroupBox *romGroup = new QGroupBox(tr("Protection ROM"));
    QVBoxLayout *romLayout = new QVBoxLayout(romGroup);
    
    QCheckBox *writeProtectCheck = new QCheckBox(tr("Protéger les ROM en écriture"));
    writeProtectCheck->setChecked(!memory->getWriteInRom());
    romLayout->addWidget(writeProtectCheck);
    
    layout->addWidget(romGroup);
    
    QGroupBox *loadGroup = new QGroupBox(tr("Chargement des ROM"));
    QVBoxLayout *loadLayout = new QVBoxLayout(loadGroup);
    
    QPushButton *loadBasicButton = new QPushButton(tr("Recharger BASIC"));
    QPushButton *loadWozButton = new QPushButton(tr("Recharger WOZ Monitor"));
    QPushButton *loadKrusaderButton = new QPushButton(tr("Recharger Krusader"));
    
    loadLayout->addWidget(loadBasicButton);
    loadLayout->addWidget(loadWozButton);
    loadLayout->addWidget(loadKrusaderButton);
    
    layout->addWidget(loadGroup);
    
    QGroupBox *memoryGroup = new QGroupBox(tr("Mémoire"));
    QVBoxLayout *memoryLayout = new QVBoxLayout(memoryGroup);
    
    QPushButton *clearMemoryButton = new QPushButton(tr("Effacer toute la mémoire"));
    QPushButton *refreshViewerButton = new QPushButton(tr("Actualiser le visualiseur"));
    
    memoryLayout->addWidget(clearMemoryButton);
    memoryLayout->addWidget(refreshViewerButton);
    
    layout->addWidget(memoryGroup);
    
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *okButton = new QPushButton(tr("OK"));
    QPushButton *cancelButton = new QPushButton(tr("Annuler"));
    
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    
    layout->addLayout(buttonLayout);
    
    // Connexions des boutons
    connect(loadBasicButton, &QPushButton::clicked, [this]() {
        memory->setWriteInRom(true);
        memory->loadBasic();
        memory->setWriteInRom(false);
        statusBar()->showMessage(tr("BASIC rechargé"), 2000);
    });
    
    connect(loadWozButton, &QPushButton::clicked, [this]() {
        memory->setWriteInRom(true);
        memory->loadWozMonitor();
        memory->setWriteInRom(false);
        statusBar()->showMessage(tr("WOZ Monitor rechargé"), 2000);
    });
    
    connect(loadKrusaderButton, &QPushButton::clicked, [this]() {
        memory->setWriteInRom(true);
        memory->loadKrusader();
        memory->setWriteInRom(false);
        statusBar()->showMessage(tr("Krusader rechargé"), 2000);
    });
    
    connect(clearMemoryButton, &QPushButton::clicked, [this]() {
        int ret = QMessageBox::warning(this, tr("Confirmation"),
            tr("Êtes-vous sûr de vouloir effacer toute la mémoire ?"),
            QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::Yes) {
            memory->resetMemory();
            for (int i = 0; i < 64*1024; i++) {
                memoryViewer->populateTable(i, 0);
            }
            statusBar()->showMessage(tr("Mémoire effacée"), 2000);
        }
    });
    
    connect(refreshViewerButton, &QPushButton::clicked, [this]() {
        for (int i = 0; i < 64*1024; i++) {
            memoryViewer->populateTable(i, memory->memRead(i));
        }
        statusBar()->showMessage(tr("Visualiseur actualisé"), 2000);
    });
    
    connect(okButton, &QPushButton::clicked, [=]() {
        memory->setWriteInRom(!writeProtectCheck->isChecked());
        configDialog->accept();
    });
    connect(cancelButton, &QPushButton::clicked, configDialog, &QDialog::reject);
    
    if (configDialog->exec() == QDialog::Accepted) {
        statusBar()->showMessage(tr("Configuration de la mémoire mise à jour"), 2000);
    }
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("About POM1"), tr("<h2>POM1 1.0 - Apple I Emulator</h2>"
                                                    "Copyright &copy; 2000-2026 GPL3."
                                                    "<ul><li>Written by <a style='color:#FF0000'>Arnaud VERHILLE</a> (2000-2026)<br>E.mail : <a href='mailto:gist974@gmail.com'>gist974@gmail.com</a><br> Web : <a href='http://gistlabs.net/Apple1project/'>Apple1 Project : Pom1</a></li>"
                                                    "<li>Enhanced by <a style='color:#FF0000'>Ken WESSEN</a> (21/2/2006)<br>Web : <a href='http://school.anhb.uwa.edu.au/personalpages/kwessen/apple1/Krusader.htm'>KRUSADER Symbolic Assembly on Replica 1</a></li>"
                                                    "<li>Ported to C/SDL by <a style='color:#FF0000'>John D. CORRADO</a> (2006-2014)<br>E.mail : <a href='mailto:jdcorrado@gmail.com'>jdcorrado@gmail.com</a><br> Web : <a href='http://pom1.sourceforge.net/'>Pom 1 for C/SDL & Android</a></li></ul"
                                                    "<p>Thanks to :"
                                                    "<ul><li><a style='color:#FF0000'>Steve WOZNIAK(The Brain) & Steve JOBS</a></li>"
                                                    "<li>Vince BRIEL ( <a href='http://www.brielcomputers.com/wordpress/?cat=17'>Replica 1 at brielcomputers.com</a> & <br><a href='https://cowgod.org/replica1/applesoft/'> Applesoft Lite: Applesoft BASIC for the Replica-1</a>)</li>"
                                                    "<li>Fabrice FRANCES (<a href='http://oric.free.fr/microtan/microtan_java.html'>Java Microtan Emulator</a>)</li>"
                                                    "<li>Achim BREIDENBACH from Boinx Software <br>(Sim6502, Online 'Apple-1 Operation Manual')</li>"
                                                    "<li>Juergen BUCHMUELLER (MAME & MESS 6502)</li>"
                                                    "<li>Stephano PRIORE from the MESS DEV</li>"
                                                    "<li>Joe TORZEWSKI(Apple I owners Club)</li>"
                                                    "<li>Tom OWAD (<a href='http://applefritter.com/apple1/'>http://applefritter.com/apple1/</a>)</li></ul>"));
}

