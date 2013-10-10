/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib/gi18n.h>

#include "menu-spec-common.h"

static const MenuSpecData msdata[] = {
	/* TRANSLATORS: this is the menu spec main category for Audio */
	{ "Audio", 				N_("Audio") },
	{ "Audio::AudioVideoEditing",		NC_("Menu subcategory of Audio", "Editing") },
	{ "Audio::Database",			NC_("Menu subcategory of Audio", "Databases") },
	{ "Audio::DiscBurning",			NC_("Menu subcategory of Audio", "Disc Burning") },
	{ "Audio::HamRadio",			NC_("Menu subcategory of Audio", "Ham Radio") },
	{ "Audio::Midi",			NC_("Menu subcategory of Audio", "MIDI") },
	{ "Audio::Mixer",			NC_("Menu subcategory of Audio", "Mixer") },
	{ "Audio::Music",			NC_("Menu subcategory of Audio", "Music") },
	{ "Audio::Player",			NC_("Menu subcategory of Audio", "Players") },
	{ "Audio::Recorder",			NC_("Menu subcategory of Audio", "Recorders") },
	{ "Audio::Sequencer",			NC_("Menu subcategory of Audio", "Sequencers") },
	{ "Audio::Tuner",			NC_("Menu subcategory of Audio", "Tuners") },
	/* TRANSLATORS: this is the menu spec main category for Development */
	{ "Development", 			N_("Development Tools") },
	{ "Development::Building",		NC_("Menu subcategory of Development", "Building") },
	{ "Development::Database",		NC_("Menu subcategory of Development", "Databases") },
	{ "Development::Debugger",		NC_("Menu subcategory of Development", "Debuggers") },
	{ "Development::GUIDesigner",		NC_("Menu subcategory of Development", "GUI Designers") },
	{ "Development::IDE",			NC_("Menu subcategory of Development", "IDE") },
	{ "Development::Profiling",		NC_("Menu subcategory of Development", "Profiling") },
	{ "Development::ProjectManagement",	NC_("Menu subcategory of Development", "Project Management") },
	{ "Development::RevisionControl",	NC_("Menu subcategory of Development", "Revision Control") },
	{ "Development::Translation",		NC_("Menu subcategory of Development", "Translation") },
	{ "Development::WebDevelopment",	NC_("Menu subcategory of Development", "Web Development") },
	/* TRANSLATORS: this is the menu spec main category for Education */
	{ "Education", 				N_("Education") },
	{ "Education::Art",			NC_("Menu subcategory of Education", "Art") },
	{ "Education::ArtificialIntelligence",	NC_("Menu subcategory of Education", "Artificial Intelligence") },
	{ "Education::Astronomy",		NC_("Menu subcategory of Education", "Astronomy") },
	{ "Education::Biology",			NC_("Menu subcategory of Education", "Biology") },
	{ "Education::Chemistry",		NC_("Menu subcategory of Education", "Chemistry") },
	{ "Education::ComputerScience",		NC_("Menu subcategory of Education", "Computer Science") },
	{ "Education::Construction",		NC_("Menu subcategory of Education", "Construction") },
	{ "Education::DataVisualization",	NC_("Menu subcategory of Education", "Data Visualization") },
	{ "Education::Economy",			NC_("Menu subcategory of Education", "Economy") },
	{ "Education::Electricity",		NC_("Menu subcategory of Education", "Electricity") },
	{ "Education::Electronics",		NC_("Menu subcategory of Education", "Electronics") },
	{ "Education::Engineering",		NC_("Menu subcategory of Education", "Engineering") },
	{ "Education::Geography",		NC_("Menu subcategory of Education", "Geography") },
	{ "Education::Geology",			NC_("Menu subcategory of Education", "Geology") },
	{ "Education::Geoscience",		NC_("Menu subcategory of Education", "Geoscience") },
	{ "Education::History",			NC_("Menu subcategory of Education", "History") },
	{ "Education::Humanities",		NC_("Menu subcategory of Education", "Humanities") },
	{ "Education::ImageProcessing",		NC_("Menu subcategory of Education", "Image Processing") },
	{ "Education::Languages",		NC_("Menu subcategory of Education", "Languages") },
	{ "Education::Literature",		NC_("Menu subcategory of Education", "Literature") },
	{ "Education::Maps",			NC_("Menu subcategory of Education", "Maps") },
	{ "Education::Math",			NC_("Menu subcategory of Education", "Math") },
	{ "Education::MedicalSoftware",		NC_("Menu subcategory of Education", "Medical") },
	{ "Education::Music",			NC_("Menu subcategory of Education", "Music") },
	{ "Education::NumericalAnalysis",	NC_("Menu subcategory of Education", "Numerical Analysis") },
	{ "Education::ParallelComputing",	NC_("Menu subcategory of Education", "Parallel Computing") },
	{ "Education::Physics",			NC_("Menu subcategory of Education", "Physics") },
	{ "Education::Robotics",		NC_("Menu subcategory of Education", "Robotics") },
	{ "Education::Spirituality",		NC_("Menu subcategory of Education", "Spirituality") },
	{ "Education::Sports",			NC_("Menu subcategory of Education", "Sports") },
	/* TRANSLATORS: this is the menu spec main category for Games */
	{ "Game", 				N_("Games") },
	{ "Game::ActionGame",			NC_("Menu subcategory of Games", "Action") },
	{ "Game::AdventureGame",		NC_("Menu subcategory of Games", "Adventure") },
	{ "Game::ArcadeGame",			NC_("Menu subcategory of Games", "Arcade") },
	{ "Game::BlocksGame",			NC_("Menu subcategory of Games", "Blocks") },
	{ "Game::BoardGame",			NC_("Menu subcategory of Games", "Board") },
	{ "Game::CardGame",			NC_("Menu subcategory of Games", "Card") },
	{ "Game::Emulator",			NC_("Menu subcategory of Games", "Emulators") },
	{ "Game::KidsGame",			NC_("Menu subcategory of Games", "Kids") },
	{ "Game::LogicGame",			NC_("Menu subcategory of Games", "Logic") },
	{ "Game::RolePlaying",			NC_("Menu subcategory of Games", "Role Playing") },
	{ "Game::Shooter",			NC_("Menu subcategory of Games", "Shooter") },
	{ "Game::Simulation",			NC_("Menu subcategory of Games", "Simulation") },
	{ "Game::SportsGame",			NC_("Menu subcategory of Games", "Sports") },
	{ "Game::StrategyGame",			NC_("Menu subcategory of Games", "Strategy") },
	/* TRANSLATORS: this is the menu spec main category for Graphics */
	{ "Graphics", 				N_("Graphics") },
	{ "Graphics::2DGraphics",		NC_("Menu subcategory of Graphics", "2D Graphics") },
	{ "Graphics::3DGraphics",		NC_("Menu subcategory of Graphics", "3D Graphics") },
	{ "Graphics::OCR",			NC_("Menu subcategory of Graphics", "OCR") },
	{ "Graphics::Photography",		NC_("Menu subcategory of Graphics", "Photography") },
	{ "Graphics::Publishing",		NC_("Menu subcategory of Graphics", "Publishing") },
	{ "Graphics::RasterGraphics",		NC_("Menu subcategory of Graphics", "Raster Graphics") },
	{ "Graphics::Scanning",			NC_("Menu subcategory of Graphics", "Scanning") },
	{ "Graphics::VectorGraphics",		NC_("Menu subcategory of Graphics", "Vector Graphics") },
	{ "Graphics::Viewer",			NC_("Menu subcategory of Graphics", "Viewer") },
	/* TRANSLATORS: this is the menu spec main category for Network */
	{ "Network", 				N_("Internet") },
	{ "Network::Chat",			NC_("Menu subcategory of Network", "Chat") },
	{ "Network::Dialup",			NC_("Menu subcategory of Network", "Dialup") },
	{ "Network::Email",			NC_("Menu subcategory of Network", "Email") },
	{ "Network::Feed",			NC_("Menu subcategory of Network", "Feed") },
	{ "Network::FileTransfer",		NC_("Menu subcategory of Network", "File Transfer") },
	{ "Network::HamRadio",			NC_("Menu subcategory of Network", "Ham Radio") },
	{ "Network::InstantMessaging",		NC_("Menu subcategory of Network", "Instant Messaging") },
	{ "Network::IRCClient",			NC_("Menu subcategory of Network", "IRC Clients") },
	{ "Network::Monitor",			NC_("Menu subcategory of Network", "Monitor") },
	{ "Network::News",			NC_("Menu subcategory of Network", "News") },
	{ "Network::P2P",			NC_("Menu subcategory of Network", "P2P") },
	{ "Network::RemoteAccess",		NC_("Menu subcategory of Network", "Remote Access") },
	{ "Network::Telephony",			NC_("Menu subcategory of Network", "Telephony") },
	{ "Network::VideoConference",		NC_("Menu subcategory of Network", "Video Conference") },
	{ "Network::WebBrowser",		NC_("Menu subcategory of Network", "Web Browser") },
	{ "Network::WebDevelopment",		NC_("Menu subcategory of Network", "Web Development") },
	/* TRANSLATORS: this is the menu spec main category for Office */
	{ "Office", 				N_("Office") },
	{ "Office::Calendar",			NC_("Menu subcategory of Office", "Calendar") },
	{ "Office::Chart",			NC_("Menu subcategory of Office", "Chart") },
	{ "Office::ContactManagement",		NC_("Menu subcategory of Office", "Contact Management") },
	{ "Office::Database",			NC_("Menu subcategory of Office", "Database") },
	{ "Office::Dictionary",			NC_("Menu subcategory of Office", "Dictionary") },
	{ "Office::Email",			NC_("Menu subcategory of Office", "Email") },
	{ "Office::Finance",			NC_("Menu subcategory of Office", "Finance") },
	{ "Office::FlowChart",			NC_("Menu subcategory of Office", "Flow Chart") },
	{ "Office::PDA",			NC_("Menu subcategory of Office", "PDA") },
	{ "Office::Photography",		NC_("Menu subcategory of Office", "Photography") },
	{ "Office::Presentation",		NC_("Menu subcategory of Office", "Presentation") },
	{ "Office::ProjectManagement",		NC_("Menu subcategory of Office", "Project Management") },
	{ "Office::Publishing",			NC_("Menu subcategory of Office", "Publishing") },
	{ "Office::Spreadsheet",		NC_("Menu subcategory of Office", "Spreadsheet") },
	{ "Office::Viewer",			NC_("Menu subcategory of Office", "Viewer") },
	{ "Office::WordProcessor",		NC_("Menu subcategory of Office", "Word Processor") },
	/* TRANSLATORS: this is the menu spec main category for Science */
	{ "Science", 				N_("Science") },
	{ "Science::Art",			NC_("Menu subcategory of Science", "Art") },
	{ "Science::ArtificialIntelligence",	NC_("Menu subcategory of Science", "Artificial Intelligence") },
	{ "Science::Astronomy",			NC_("Menu subcategory of Science", "Astronomy") },
	{ "Science::Biology",			NC_("Menu subcategory of Science", "Biology") },
	{ "Science::Chemistry",			NC_("Menu subcategory of Science", "Chemistry") },
	{ "Science::ComputerScience",		NC_("Menu subcategory of Science", "Computer Science") },
	{ "Science::Construction",		NC_("Menu subcategory of Science", "Construction") },
	{ "Science::DataVisualization",		NC_("Menu subcategory of Science", "Data Visualization") },
	{ "Science::Economy",			NC_("Menu subcategory of Science", "Economy") },
	{ "Science::Electricity",		NC_("Menu subcategory of Science", "Electricity") },
	{ "Science::Electronics",		NC_("Menu subcategory of Science", "Electronics") },
	{ "Science::Engineering",		NC_("Menu subcategory of Science", "Engineering") },
	{ "Science::Geography",			NC_("Menu subcategory of Science", "Geography") },
	{ "Science::Geology",			NC_("Menu subcategory of Science", "Geology") },
	{ "Science::Geoscience",		NC_("Menu subcategory of Science", "Geoscience") },
	{ "Science::History",			NC_("Menu subcategory of Science", "History") },
	{ "Science::Humanities",		NC_("Menu subcategory of Science", "Humanities") },
	{ "Science::ImageProcessing",		NC_("Menu subcategory of Science", "Image Processing") },
	{ "Science::Languages",			NC_("Menu subcategory of Science", "Languages") },
	{ "Science::Literature",		NC_("Menu subcategory of Science", "Literature") },
	{ "Science::Maps",			NC_("Menu subcategory of Science", "Maps") },
	{ "Science::Math",			NC_("Menu subcategory of Science", "Math") },
	{ "Science::MedicalSoftware",		NC_("Menu subcategory of Science", "Medical") },
	{ "Science::NumericalAnalysis",		NC_("Menu subcategory of Science", "Numerical Analysis") },
	{ "Science::ParallelComputing",		NC_("Menu subcategory of Science", "Parallel Computing") },
	{ "Science::Physics",			NC_("Menu subcategory of Science", "Physics") },
	{ "Science::Robotics",			NC_("Menu subcategory of Science", "Robotics") },
	{ "Science::Spirituality",		NC_("Menu subcategory of Science", "Spirituality") },
	{ "Science::Sports",			NC_("Menu subcategory of Science", "Sports") },
	/* TRANSLATORS: this is the menu spec main category for System */
	{ "System", 				N_("System") },
	{ "System::Emulator",			NC_("Menu subcategory of System", "Emulator") },
	{ "System::FileManager",		NC_("Menu subcategory of System", "File Manager") },
	{ "System::Filesystem",			NC_("Menu subcategory of System", "File System") },
	{ "System::FileTools",			NC_("Menu subcategory of System", "File Tools") },
	{ "System::Monitor",			NC_("Menu subcategory of System", "Monitor") },
	{ "System::Security",			NC_("Menu subcategory of System", "Security") },
	{ "System::TerminalEmulator",		NC_("Menu subcategory of System", "Terminal Emulator") },
	/* TRANSLATORS: this is the menu spec main category for Utility */
	{ "Utility", 				N_("Utilities") },
	{ "Utility::Accessibility",		NC_("Menu subcategory of Utility", "Accessibility") },
	{ "Utility::Archiving",			NC_("Menu subcategory of Utility", "Archiving") },
	{ "Utility::Calculator",		NC_("Menu subcategory of Utility", "Calculator") },
	{ "Utility::Clock",			NC_("Menu subcategory of Utility", "Clock") },
	{ "Utility::Compression",		NC_("Menu subcategory of Utility", "Compression") },
	{ "Utility::FileTools",			NC_("Menu subcategory of Utility", "File Tools") },
	{ "Utility::Maps",			NC_("Menu subcategory of Utility", "Maps") },
	{ "Utility::Spirituality",		NC_("Menu subcategory of Utility", "Spirituality") },
	{ "Utility::TelephonyTools",		NC_("Menu subcategory of Utility", "Telephony Tools") },
	{ "Utility::TextEditor",		NC_("Menu subcategory of Utility", "Text Editor") },
	/* TRANSLATORS: this is the menu spec main category for Video */
	{ "Video", 				N_("Video") },
	{ "Video::AudioVideoEditing",		NC_("Menu subcategory of Video", "Editing") },
	{ "Video::Database",			NC_("Menu subcategory of Video", "Database") },
	{ "Video::DiscBurning",			NC_("Menu subcategory of Video", "Disc Burning") },
	{ "Video::Player",			NC_("Menu subcategory of Video", "Players") },
	{ "Video::Recorder",			NC_("Menu subcategory of Video", "Recorders") },
	{ "Video::TV",				NC_("Menu subcategory of Video", "TV") },
	/* TRANSLATORS: this is the main category for Add-ons */
	{ "Addons", 				N_("Add-ons") },
	{ "Addons::Fonts",			NC_("Menu subcategory of Addons", "Fonts") },
	{ "Addons::Codecs",			NC_("Menu subcategory of Addons", "Codecs") },
	{ "Addons::InputSources",		NC_("Menu subcategory of Addons", "Input Sources") },
	{ "Addons::LanguagePacks",		NC_("Menu subcategory of Addons", "Language Packs") },
	{ NULL,					NULL }
};

/**
 * menu_spec_get_data:
 */
const MenuSpecData *
menu_spec_get_data (void)
{
	return msdata;
}
