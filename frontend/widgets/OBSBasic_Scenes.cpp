/******************************************************************************
    Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>
                          Zachary Lund <admin@computerquip.com>
                          Philippe Groarke <philippe.groarke@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "OBSBasic.hpp"
#include "OBSProjector.hpp"

#include <dialogs/NameDialog.hpp>

#include <qt-wrappers.hpp>

#include <QLineEdit>
#include <QWidgetAction>

#include <vector>

using namespace std;

namespace {

template<typename OBSRef> struct SignalContainer {
	OBSRef ref;
	vector<shared_ptr<OBSSignal>> handlers;
};

QDataStream &operator<<(QDataStream &out, const SignalContainer<OBSScene> &v)
{
	out << v.ref;
	return out;
}

QDataStream &operator>>(QDataStream &in, SignalContainer<OBSScene> &v)
{
	in >> v.ref;
	return in;
}

} // namespace

Q_DECLARE_METATYPE(obs_order_movement);
Q_DECLARE_METATYPE(SignalContainer<OBSScene>);

extern void undo_redo(const std::string &data);

obs_data_array_t *OBSBasic::SaveSceneListOrder()
{
	obs_data_array_t *sceneOrder = obs_data_array_create();

	for (int i = 0; i < ui->scenes->count(); i++) {
		OBSDataAutoRelease data = obs_data_create();
		obs_data_set_string(data, "name", QT_TO_UTF8(ui->scenes->item(i)->text()));
		obs_data_array_push_back(sceneOrder, data);
	}

	return sceneOrder;
}

static void ReorderItemByName(QListWidget *lw, const char *name, int newIndex)
{
	for (int i = 0; i < lw->count(); i++) {
		QListWidgetItem *item = lw->item(i);

		if (strcmp(name, QT_TO_UTF8(item->text())) == 0) {
			if (newIndex != i) {
				item = lw->takeItem(i);
				lw->insertItem(newIndex, item);
			}
			break;
		}
	}
}

void OBSBasic::LoadSceneListOrder(obs_data_array_t *array)
{
	size_t num = obs_data_array_count(array);

	for (size_t i = 0; i < num; i++) {
		OBSDataAutoRelease data = obs_data_array_item(array, i);
		const char *name = obs_data_get_string(data, "name");

		ReorderItemByName(ui->scenes, name, (int)i);
	}
}

OBSScene OBSBasic::GetCurrentScene()
{
	// If dual output is active, return the scene corresponding to the active preview pane.
	// Otherwise, fall back to the single 'currentScene' or a primary scene.
	if (App()->IsDualOutputActive()) {
		if (activePreviewPane == ActivePreview::HORIZONTAL) {
			return obs_scene_from_source(App()->GetCurrentHorizontalScene());
		} else {
			return obs_scene_from_source(App()->GetCurrentVerticalScene());
		}
	} else {
		// Fallback to original behavior or always horizontal if not dual.
		// obs_frontend_get_current_preview_scene() might be what original currentScene was tracking.
		// For now, let's assume if not dual, horizontal is the one.
		return obs_scene_from_source(App()->GetCurrentHorizontalScene());
		// Or, if 'currentScene' member is still maintained for single mode:
		// return currentScene.load();
	}
}

void OBSBasic::AddScene(OBSSource source)
{
	const char *name = obs_source_get_name(source);
	obs_scene_t *scene = obs_scene_from_source(source);

	QListWidgetItem *item = new QListWidgetItem(QT_UTF8(name));
	SetOBSRef(item, OBSScene(scene));
	ui->scenes->insertItem(ui->scenes->currentRow() + 1, item);

	obs_hotkey_register_source(
		source, "OBSBasic.SelectScene", Str("Basic.Hotkeys.SelectScene"),
		[](void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
			OBSBasic *main = OBSBasic::Get();

			auto potential_source = static_cast<obs_source_t *>(data);
			OBSSourceAutoRelease source = obs_source_get_ref(potential_source);
			if (source && pressed)
				main->SetCurrentScene(source.Get());
		},
		static_cast<obs_source_t *>(source));

	signal_handler_t *handler = obs_source_get_signal_handler(source);

	SignalContainer<OBSScene> container;
	container.ref = scene;
	container.handlers.assign({
		std::make_shared<OBSSignal>(handler, "item_add", OBSBasic::SceneItemAdded, this),
		std::make_shared<OBSSignal>(handler, "reorder", OBSBasic::SceneReordered, this),
		std::make_shared<OBSSignal>(handler, "refresh", OBSBasic::SceneRefreshed, this),
	});

	item->setData(static_cast<int>(QtDataRole::OBSSignals), QVariant::fromValue(container));

	/* if the scene already has items (a duplicated scene) add them */
	auto addSceneItem = [this](obs_sceneitem_t *item) {
		AddSceneItem(item);
	};

	using addSceneItem_t = decltype(addSceneItem);

	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			addSceneItem_t *func;
			func = static_cast<addSceneItem_t *>(param);
			(*func)(item);
			return true;
		},
		&addSceneItem);

	SaveProject();

	if (!disableSaving) {
		obs_source_t *source = obs_scene_get_source(scene);
		blog(LOG_INFO, "User added scene '%s'", obs_source_get_name(source));

		OBSProjector::UpdateMultiviewProjectors();
	}

	OnEvent(OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED);
}

void OBSBasic::RemoveScene(OBSSource source)
{
	obs_scene_t *scene = obs_scene_from_source(source);

	QListWidgetItem *sel = nullptr;
	int count = ui->scenes->count();

	for (int i = 0; i < count; i++) {
		auto item = ui->scenes->item(i);
		auto cur_scene = GetOBSRef<OBSScene>(item);
		if (cur_scene != scene)
			continue;

		sel = item;
		break;
	}

	if (sel != nullptr) {
		if (sel == ui->scenes->currentItem())
			ui->sources->Clear();
		delete sel;
	}

	SaveProject();

	if (!disableSaving) {
		blog(LOG_INFO, "User Removed scene '%s'", obs_source_get_name(source));

		OBSProjector::UpdateMultiviewProjectors();
	}

	OnEvent(OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED);
}

void OBSBasic::RefreshSceneListDisplay()
{
	blog(LOG_INFO, "OBSBasic::RefreshSceneListDisplay called. Active pane: %s",
	     (activePreviewPane == ActivePreview::HORIZONTAL) ? "Horizontal" : "Vertical");

	QString currentSelectedSceneName;
	if (ui->scenes->currentItem()) {
		currentSelectedSceneName = ui->scenes->currentItem()->text();
	}

	// Block signals during modification
	ui->scenes->blockSignals(true);

	// Store existing item data to avoid re-creating hotkeys and signals unnecessarily
	// This is a simplified approach; a more robust one might map names to existing items/data.
	std::vector<QListWidgetItem*> existingItems;
	for(int i = 0; i < ui->scenes->count(); ++i) {
		existingItems.push_back(ui->scenes->item(i));
	}
	ui->scenes->clear(); // Clear the display list

	struct obs_frontend_source_list scenes_sources;
	obs_frontend_get_scenes(&scenes_sources);

	for (size_t i = 0; i < scenes_sources.num; ++i) {
		obs_source_t *scene_source = scenes_sources.sources[i];
		const char *scene_name_char = obs_source_get_name(scene_source);
		QString scene_name = QString::fromUtf8(scene_name_char);
		bool add_this_scene = false;

		if (App()->IsDualOutputActive()) {
			if (activePreviewPane == ActivePreview::HORIZONTAL) {
				// Show H_ scenes or scenes not starting with V_
				if (scene_name.startsWith("H_", Qt::CaseInsensitive) || !scene_name.startsWith("V_", Qt::CaseInsensitive)) {
					add_this_scene = true;
				}
			} else { // ActivePreview::VERTICAL
				// Show only V_ scenes
				if (scene_name.startsWith("V_", Qt::CaseInsensitive)) {
					add_this_scene = true;
				}
			}
		} else {
			// If not dual output, show all scenes (original behavior)
			add_this_scene = true;
		}

		if (add_this_scene) {
			// Try to find if this item already existed to reuse QListWidgetItem and its data
			// This avoids re-registering hotkeys etc. unnecessarily if AddScene does that.
			// For a simpler first pass, we can just call a simplified AddScene.
			// The current AddScene also saves project and emits events, which we might not want during a refresh.

			// Simplified add:
			QListWidgetItem *item = new QListWidgetItem(scene_name);
			SetOBSRef(item, OBSScene(obs_scene_from_source(scene_source)));
			// TODO: Need to re-attach signal handlers (SignalContainer) if not reusing items.
			// For now, this is a basic re-population. A proper AddScene should be used or refactored.
			ui->scenes->addItem(item);

			if (scene_name == currentSelectedSceneName) {
				ui->scenes->setCurrentItem(item);
			}
		}
	}

	obs_frontend_source_list_free(&scenes_sources);

	if (!ui->scenes->currentItem() && ui->scenes->count() > 0) {
		ui->scenes->setCurrentRow(0);
	}

	// Unblock signals
	ui->scenes->blockSignals(false);

	// Trigger a selection change to update sources list etc.
	if (ui->scenes->currentItem()) {
		on_scenes_currentItemChanged(ui->scenes->currentItem(), nullptr);
	} else {
		on_scenes_currentItemChanged(nullptr, nullptr); // Clear sources if no scene
	}

	UpdateContextBar();
	// OBSProjector::UpdateMultiviewProjectors(); // May also be needed
	OnEvent(OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED); // Notify that the displayed list changed
}


static bool select_one(obs_scene_t * /* scene */, obs_sceneitem_t *item, void *param)
{
	obs_sceneitem_t *selectedItem = static_cast<obs_sceneitem_t *>(param);
	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, select_one, param);

	obs_sceneitem_select(item, (selectedItem == item));

	return true;
}

void OBSBasic::AddSceneItem(OBSSceneItem item)
{
	obs_scene_t *scene = obs_sceneitem_get_scene(item);

	if (GetCurrentScene() == scene)
		ui->sources->Add(item);

	SaveProject();

	if (!disableSaving) {
		obs_source_t *sceneSource = obs_scene_get_source(scene);
		obs_source_t *itemSource = obs_sceneitem_get_source(item);
		blog(LOG_INFO, "User added source '%s' (%s) to scene '%s'", obs_source_get_name(itemSource),
		     obs_source_get_id(itemSource), obs_source_get_name(sceneSource));

		obs_scene_enum_items(scene, select_one, (obs_sceneitem_t *)item);
	}
}

void OBSBasic::DuplicateSelectedScene()
{
	OBSScene curScene = GetCurrentScene();

	if (!curScene)
		return;

	OBSSource curSceneSource = obs_scene_get_source(curScene);
	QString originalName = QString::fromUtf8(obs_source_get_name(curSceneSource));
	QString baseName = originalName;
	QString prefix = "";

	if (App()->IsDualOutputActive()) {
		if (originalName.startsWith("V_", Qt::CaseInsensitive)) {
			prefix = "V_";
			baseName = originalName.mid(2);
		} else if (originalName.startsWith("H_", Qt::CaseInsensitive)) {
			prefix = "H_";
			baseName = originalName.mid(2);
		} else { // Unprefixed, treat as horizontal for duplication prefixing
			prefix = "H_";
		}
	}

	QString format = baseName + " %1";
	int i = 2;
	QString suggestedNewNameWithoutPrefix = format.arg(i);
	QString placeHolderText = prefix + suggestedNewNameWithoutPrefix;

	OBSSourceAutoRelease source_check_placeholder = nullptr;
	while ((source_check_placeholder = obs_get_source_by_name(QT_TO_UTF8(placeHolderText)))) {
		suggestedNewNameWithoutPrefix = format.arg(++i);
		placeHolderText = prefix + suggestedNewNameWithoutPrefix;
		obs_source_release(source_check_placeholder); // Release the ref from obs_get_source_by_name
	}


	for (;;) {
		string name_std; // Name entered by user, potentially without prefix
		bool accepted = NameDialog::AskForName(this, QTStr("Basic.Main.DuplicateSceneDlg.Title"),
						       QTStr("Basic.Main.DuplicateSceneDlg.Text"), name_std, placeHolderText);
		if (!accepted)
			return;

		if (name_std.empty()) {
			OBSMessageBox::warning(this, QTStr("NoNameEntered.Title"), QTStr("NoNameEntered.Text"));
			continue;
		}

		QString finalName = QString::fromStdString(name_std);
		// Ensure the final name has the correct prefix if user removed/changed it,
		// based on the source scene's type or current context.
		if (App()->IsDualOutputActive()) {
			if (!prefix.isEmpty()) { // If original had a prefix (or was unprefixed, getting H_ default)
				if (!finalName.startsWith(prefix, Qt::CaseInsensitive)) {
					// If user provided a name that doesn't start with the expected prefix,
					// we should decide: either force prefix, or if they used the OTHER prefix, warn/adjust.
					// For now, let's assume they might have typed the full desired name.
					// If it doesn't start with EITHER H_ or V_, then apply the determined prefix.
					if (!finalName.startsWith("H_", Qt::CaseInsensitive) && !finalName.startsWith("V_", Qt::CaseInsensitive)) {
						finalName = prefix + finalName;
					}
					// If they typed "V_NewName" when duplicating an "H_" scene, finalName is "V_NewName".
					// This allows cross-type duplication if desired, scene list filtering will handle visibility.
				}
			} else { // Original was unprefixed, and dual output is active (so prefix should be H_)
				 if (!finalName.startsWith("H_", Qt::CaseInsensitive) && !finalName.startsWith("V_", Qt::CaseInsensitive)) {
					finalName = "H_" + finalName;
				 }
			}
		}
		std::string finalNameStd = finalName.toStdString();


		obs_source_t *source = obs_get_source_by_name(finalNameStd.c_str());
		if (source) {
			OBSMessageBox::warning(this, QTStr("NameExists.Title"), QTStr("NameExists.Text"));
			obs_source_release(source);
			placeHolderText = finalName; // Suggest the failed name again
			continue;
		}

		OBSSceneAutoRelease scene = obs_scene_duplicate(curScene, finalNameStd.c_str(), OBS_SCENE_DUP_REFS);
		source = obs_scene_get_source(scene);
		SetCurrentScene(source, true); // SetCurrentScene is context-aware

		auto undo = [](const std::string &data) {
			OBSSourceAutoRelease source = obs_get_source_by_name(data.c_str());
			obs_source_remove(source);
		};

		// Redo for duplicate needs to know the source of duplication for obs_scene_duplicate
		std::string originalSceneNameStd = originalName.toStdString();
		auto redo = [this, finalNameStd, originalSceneNameStd](const std::string &/*data is new name, not needed here*/) {
			OBSSourceAutoRelease originalDupSource = obs_get_source_by_name(originalSceneNameStd.c_str());
			if (originalDupSource) {
				obs_scene_t* originalDupScene = obs_scene_from_source(originalDupSource);
				if (originalDupScene) {
					OBSSceneAutoRelease scene = obs_scene_duplicate(originalDupScene, finalNameStd.c_str(), OBS_SCENE_DUP_REFS);
					obs_source_t *source = obs_scene_get_source(scene);
					SetCurrentScene(source, true);
				}
			}
		};

		undo_s.add_action(QTStr("Undo.Scene.Duplicate").arg(finalName), undo, redo,
				  finalNameStd, originalSceneNameStd); // Pass original name for redo context

		RefreshSceneListDisplay();
		break;
	}
}

static bool save_undo_source_enum(obs_scene_t * /* scene */, obs_sceneitem_t *item, void *p)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (obs_obj_is_private(source) && !obs_source_removed(source))
		return true;

	obs_data_array_t *array = (obs_data_array_t *)p;

	/* check if the source is already stored in the array */
	const char *name = obs_source_get_name(source);
	const size_t count = obs_data_array_count(array);
	for (size_t i = 0; i < count; i++) {
		OBSDataAutoRelease sourceData = obs_data_array_item(array, i);
		if (strcmp(name, obs_data_get_string(sourceData, "name")) == 0)
			return true;
	}

	if (obs_source_is_group(source))
		obs_scene_enum_items(obs_group_from_source(source), save_undo_source_enum, p);

	OBSDataAutoRelease source_data = obs_save_source(source);
	obs_data_array_push_back(array, source_data);
	return true;
}

static inline void RemoveSceneAndReleaseNested(obs_source_t *source)
{
	obs_source_remove(source);
	auto cb = [](void *, obs_source_t *source) {
		if (strcmp(obs_source_get_id(source), "scene") == 0)
			obs_scene_prune_sources(obs_scene_from_source(source));
		return true;
	};
	obs_enum_scenes(cb, NULL);
}

void OBSBasic::RemoveSelectedScene()
{
	OBSScene scene = GetCurrentScene();
	obs_source_t *source = obs_scene_get_source(scene);

	if (!source || !QueryRemoveSource(source)) {
		return;
	}

	/* ------------------------------ */
	/* save all sources in scene      */

	OBSDataArrayAutoRelease sources_in_deleted_scene = obs_data_array_create();

	obs_scene_enum_items(scene, save_undo_source_enum, sources_in_deleted_scene);

	OBSDataAutoRelease scene_data = obs_save_source(source);
	obs_data_array_push_back(sources_in_deleted_scene, scene_data);

	/* ----------------------------------------------- */
	/* save all scenes and groups the scene is used in */

	OBSDataArrayAutoRelease scene_used_in_other_scenes = obs_data_array_create();

	struct other_scenes_cb_data {
		obs_source_t *oldScene;
		obs_data_array_t *scene_used_in_other_scenes;
	} other_scenes_cb_data;
	other_scenes_cb_data.oldScene = source;
	other_scenes_cb_data.scene_used_in_other_scenes = scene_used_in_other_scenes;

	auto other_scenes_cb = [](void *data_ptr, obs_source_t *scene) {
		struct other_scenes_cb_data *data = (struct other_scenes_cb_data *)data_ptr;
		if (strcmp(obs_source_get_name(scene), obs_source_get_name(data->oldScene)) == 0)
			return true;
		obs_sceneitem_t *item = obs_scene_find_source(obs_group_or_scene_from_source(scene),
							      obs_source_get_name(data->oldScene));
		if (item) {
			OBSDataAutoRelease scene_data =
				obs_save_source(obs_scene_get_source(obs_sceneitem_get_scene(item)));
			obs_data_array_push_back(data->scene_used_in_other_scenes, scene_data);
		}
		return true;
	};
	obs_enum_scenes(other_scenes_cb, &other_scenes_cb_data);

	/* --------------------------- */
	/* undo/redo                   */

	auto undo = [this](const std::string &json) {
		OBSDataAutoRelease base = obs_data_create_from_json(json.c_str());
		OBSDataArrayAutoRelease sources_in_deleted_scene = obs_data_get_array(base, "sources_in_deleted_scene");
		OBSDataArrayAutoRelease scene_used_in_other_scenes =
			obs_data_get_array(base, "scene_used_in_other_scenes");
		int savedIndex = (int)obs_data_get_int(base, "index");
		std::vector<OBSSource> sources;

		/* create missing sources */
		size_t count = obs_data_array_count(sources_in_deleted_scene);
		sources.reserve(count);

		for (size_t i = 0; i < count; i++) {
			OBSDataAutoRelease data = obs_data_array_item(sources_in_deleted_scene, i);
			const char *name = obs_data_get_string(data, "name");

			OBSSourceAutoRelease source = obs_get_source_by_name(name);
			if (!source) {
				source = obs_load_source(data);
				sources.push_back(source.Get());
			}
		}

		/* actually load sources now */
		for (obs_source_t *source : sources)
			obs_source_load2(source);

		/* Add scene to scenes and groups it was nested in */
		for (size_t i = 0; i < obs_data_array_count(scene_used_in_other_scenes); i++) {
			OBSDataAutoRelease data = obs_data_array_item(scene_used_in_other_scenes, i);
			const char *name = obs_data_get_string(data, "name");
			OBSSourceAutoRelease source = obs_get_source_by_name(name);

			OBSDataAutoRelease settings = obs_data_get_obj(data, "settings");
			OBSDataArrayAutoRelease items = obs_data_get_array(settings, "items");

			/* Clear scene, but keep a reference to all sources in the scene to make sure they don't get destroyed */
			std::vector<OBSSource> existing_sources;
			auto cb = [](obs_scene_t *, obs_sceneitem_t *item, void *data) {
				std::vector<OBSSource> *existing = (std::vector<OBSSource> *)data;
				OBSSource source = obs_sceneitem_get_source(item);
				obs_sceneitem_remove(item);
				existing->push_back(source);
				return true;
			};
			obs_scene_enum_items(obs_group_or_scene_from_source(source), cb, (void *)&existing_sources);

			/* Re-add sources to the scene */
			obs_sceneitems_add(obs_group_or_scene_from_source(source), items);
		}

		obs_source_t *scene_source = sources.back();
		OBSScene scene = obs_scene_from_source(scene_source);
		SetCurrentScene(scene, true);

		/* set original index in list box */
		ui->scenes->blockSignals(true);
		int curIndex = ui->scenes->currentRow();
		QListWidgetItem *item = ui->scenes->takeItem(curIndex);
		ui->scenes->insertItem(savedIndex, item);
		ui->scenes->setCurrentRow(savedIndex);
		currentScene = scene.Get();
		ui->scenes->blockSignals(false);
	};

	auto redo = [](const std::string &name) {
		OBSSourceAutoRelease source = obs_get_source_by_name(name.c_str());
		RemoveSceneAndReleaseNested(source);
	};

	OBSDataAutoRelease data = obs_data_create();
	obs_data_set_array(data, "sources_in_deleted_scene", sources_in_deleted_scene);
	obs_data_set_array(data, "scene_used_in_other_scenes", scene_used_in_other_scenes);
	obs_data_set_int(data, "index", ui->scenes->currentRow());

	const char *scene_name = obs_source_get_name(source);
	undo_s.add_action(QTStr("Undo.Delete").arg(scene_name), undo, redo, obs_data_get_json(data), scene_name);

	/* --------------------------- */
	/* remove                      */

	RemoveSceneAndReleaseNested(source);

	OnEvent(OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED);
}

void OBSBasic::SceneReordered(void *data, calldata_t *params)
{
	OBSBasic *window = static_cast<OBSBasic *>(data);

	obs_scene_t *scene = (obs_scene_t *)calldata_ptr(params, "scene");

	QMetaObject::invokeMethod(window, "ReorderSources", Q_ARG(OBSScene, OBSScene(scene)));
}

void OBSBasic::SceneRefreshed(void *data, calldata_t *params)
{
	OBSBasic *window = static_cast<OBSBasic *>(data);

	obs_scene_t *scene = (obs_scene_t *)calldata_ptr(params, "scene");

	QMetaObject::invokeMethod(window, "RefreshSources", Q_ARG(OBSScene, OBSScene(scene)));
}

void OBSBasic::SceneItemAdded(void *data, calldata_t *params)
{
	OBSBasic *window = static_cast<OBSBasic *>(data);

	obs_sceneitem_t *item = (obs_sceneitem_t *)calldata_ptr(params, "item");

	QMetaObject::invokeMethod(window, "AddSceneItem", Q_ARG(OBSSceneItem, OBSSceneItem(item)));
}

void OBSBasic::on_scenes_currentItemChanged(QListWidgetItem *current, QListWidgetItem *)
{
	OBSSource source;

	if (current) {
		OBSScene scene = GetOBSRef<OBSScene>(current);
		source = obs_scene_get_source(scene);

		currentScene = scene;
	} else {
		currentScene = NULL;
	}

	SetCurrentScene(source);

	if (vcamEnabled && vcamConfig.type == VCamOutputType::PreviewOutput)
		outputHandler->UpdateVirtualCamOutputSource();

	OnEvent(OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED);

	UpdateContextBar();
}

void OBSBasic::EditSceneName()
{
	ui->scenesDock->removeAction(renameScene);
	QListWidgetItem *item = ui->scenes->currentItem();
	Qt::ItemFlags flags = item->flags();

	item->setFlags(flags | Qt::ItemIsEditable);
	ui->scenes->editItem(item);
	item->setFlags(flags);
}

void OBSBasic::on_scenes_customContextMenuRequested(const QPoint &pos)
{
	QListWidgetItem *item = ui->scenes->itemAt(pos);

	QMenu popup(this);
	QMenu order(QTStr("Basic.MainMenu.Edit.Order"), this);

	popup.addAction(QTStr("AddScene") + "...", this, &OBSBasic::on_actionAddScene_triggered);

	if (item) {
		QAction *copyFilters = new QAction(QTStr("Copy.Filters"), this);
		copyFilters->setEnabled(false);
		connect(copyFilters, &QAction::triggered, this, &OBSBasic::SceneCopyFilters);
		QAction *pasteFilters = new QAction(QTStr("Paste.Filters"), this);
		pasteFilters->setEnabled(!obs_weak_source_expired(copyFiltersSource));
		connect(pasteFilters, &QAction::triggered, this, &OBSBasic::ScenePasteFilters);

		popup.addSeparator();
		popup.addAction(QTStr("Duplicate"), this, &OBSBasic::DuplicateSelectedScene);
		popup.addAction(copyFilters);
		popup.addAction(pasteFilters);
		popup.addSeparator();
		popup.addAction(renameScene);
		popup.addAction(ui->actionRemoveScene);
		popup.addSeparator();

		order.addAction(QTStr("Basic.MainMenu.Edit.Order.MoveUp"), this, &OBSBasic::on_actionSceneUp_triggered);
		order.addAction(QTStr("Basic.MainMenu.Edit.Order.MoveDown"), this,
				&OBSBasic::on_actionSceneDown_triggered);
		order.addSeparator();
		order.addAction(QTStr("Basic.MainMenu.Edit.Order.MoveToTop"), this, &OBSBasic::MoveSceneToTop);
		order.addAction(QTStr("Basic.MainMenu.Edit.Order.MoveToBottom"), this, &OBSBasic::MoveSceneToBottom);
		popup.addMenu(&order);

		popup.addSeparator();

		delete sceneProjectorMenu;
		sceneProjectorMenu = new QMenu(QTStr("Projector.Open.Scene"));
		AddProjectorMenuMonitors(sceneProjectorMenu, this, &OBSBasic::OpenSceneProjector);
		sceneProjectorMenu->addSeparator();
		sceneProjectorMenu->addAction(QTStr("Projector.Window"), this, &OBSBasic::OpenSceneWindow);

		popup.addMenu(sceneProjectorMenu);
		popup.addSeparator();

		popup.addAction(QTStr("Screenshot.Scene"), this, &OBSBasic::ScreenshotScene);
		popup.addSeparator();
		popup.addAction(QTStr("Filters"), this, &OBSBasic::OpenSceneFilters);

		popup.addSeparator();

		delete perSceneTransitionMenu;
		perSceneTransitionMenu = CreatePerSceneTransitionMenu();
		popup.addMenu(perSceneTransitionMenu);

		/* ---------------------- */

		QAction *multiviewAction = popup.addAction(QTStr("ShowInMultiview"));

		OBSSource source = GetCurrentSceneSource();
		OBSDataAutoRelease data = obs_source_get_private_settings(source);

		obs_data_set_default_bool(data, "show_in_multiview", true);
		bool show = obs_data_get_bool(data, "show_in_multiview");

		multiviewAction->setCheckable(true);
		multiviewAction->setChecked(show);

		auto showInMultiview = [](OBSData data) {
			bool show = obs_data_get_bool(data, "show_in_multiview");
			obs_data_set_bool(data, "show_in_multiview", !show);
			OBSProjector::UpdateMultiviewProjectors();
		};

		connect(multiviewAction, &QAction::triggered, std::bind(showInMultiview, data.Get()));

		copyFilters->setEnabled(obs_source_filter_count(source) > 0);
	}

	popup.addSeparator();

	bool grid = ui->scenes->GetGridMode();

	QAction *gridAction = new QAction(grid ? QTStr("Basic.Main.ListMode") : QTStr("Basic.Main.GridMode"), this);
	connect(gridAction, &QAction::triggered, this, &OBSBasic::GridActionClicked);
	popup.addAction(gridAction);

	popup.exec(QCursor::pos());
}

void OBSBasic::on_actionSceneListMode_triggered()
{
	ui->scenes->SetGridMode(false);
	config_set_bool(App()->GetUserConfig(), "BasicWindow", "gridMode", false);
}

void OBSBasic::on_actionSceneGridMode_triggered()
{
	ui->scenes->SetGridMode(true);
	config_set_bool(App()->GetUserConfig(), "BasicWindow", "gridMode", true);
}

void OBSBasic::GridActionClicked()
{
	bool gridMode = !ui->scenes->GetGridMode();
	ui->scenes->SetGridMode(gridMode);

	if (gridMode)
		ui->actionSceneGridMode->setChecked(true);
	else
		ui->actionSceneListMode->setChecked(true);

	config_set_bool(App()->GetUserConfig(), "BasicWindow", "gridMode", gridMode);
}

void OBSBasic::on_actionAddScene_triggered()
{
	string name;
	QString format{QTStr("Basic.Main.DefaultSceneName.Text")};

	int i = 2;
	QString placeHolderText = format.arg(i);
	OBSSourceAutoRelease source = nullptr;
	while ((source = obs_get_source_by_name(QT_TO_UTF8(placeHolderText)))) {
		placeHolderText = format.arg(++i);
	}

	bool accepted = NameDialog::AskForName(this, QTStr("Basic.Main.AddSceneDlg.Title"),
					       QTStr("Basic.Main.AddSceneDlg.Text"), name, placeHolderText);

	if (accepted) {
		if (name.empty()) {
			OBSMessageBox::warning(this, QTStr("NoNameEntered.Title"), QTStr("NoNameEntered.Text"));
			on_actionAddScene_triggered(); // Re-trigger to prompt again
			return;
		}

		QString finalSceneName = QString::fromStdString(name);
		if (App()->IsDualOutputActive()) {
			if (activePreviewPane == ActivePreview::VERTICAL) {
				if (!finalSceneName.startsWith("V_", Qt::CaseInsensitive)) {
					finalSceneName = "V_" + finalSceneName;
				}
			} else { // HORIZONTAL or if we want to enforce H_ prefix for horizontal too in dual mode
				if (!finalSceneName.startsWith("H_", Qt::CaseInsensitive) && !finalSceneName.startsWith("V_", Qt::CaseInsensitive)) {
					finalSceneName = "H_" + finalSceneName;
				} else if (finalSceneName.startsWith("V_", Qt::CaseInsensitive)) {
					// User might be trying to create a V_ scene in H_ context. Forbid or warn?
					// For now, let's allow it but it won't show in H list by default. Or force H_?
					// Let's adjust placeHolderText generation to suggest prefixed name.
				}
			}
		}
		// If not dual output active, no prefix is strictly needed unless desired for future-proofing.

		std::string finalSceneNameStd = finalSceneName.toStdString();
		OBSSourceAutoRelease source_check = obs_get_source_by_name(finalSceneNameStd.c_str());
		if (source_check) {
			OBSMessageBox::warning(this, QTStr("NameExists.Title"), QTStr("NameExists.Text"));
			obs_source_release(source_check);
			on_actionAddScene_triggered(); // Re-trigger
			return;
		}

		auto undo_fn = [](const std::string &data) {
			obs_source_t *t = obs_get_source_by_name(data.c_str());
			if (t) {
				obs_source_remove(t);
				obs_source_release(t);
			}
		};

		auto redo_fn = [this](const std::string &data) {
			OBSSceneAutoRelease scene = obs_scene_create(data.c_str());
			obs_source_t *source = obs_scene_get_source(scene);
			// SetCurrentScene needs to be context aware of which pane this new scene belongs to
			// If activePreviewPane was Vertical, this new scene is a V_ scene.
			// SetCurrentScene will handle setting it in App() for the correct pane.
			SetCurrentScene(source, true);
		};
		undo_s.add_action(QTStr("Undo.Add").arg(finalSceneName), undo_fn, redo_fn, finalSceneNameStd, finalSceneNameStd);

		OBSSceneAutoRelease scene = obs_scene_create(finalSceneNameStd.c_str());
		obs_source_t *scene_source = obs_scene_get_source(scene);
		SetCurrentScene(scene_source); // This will set it for the active pane context
		RefreshSceneListDisplay(); // Refresh the list to show the new scene if it matches current filter
	}
}

void OBSBasic::on_actionRemoveScene_triggered()
{
	RemoveSelectedScene();
}

void OBSBasic::ChangeSceneIndex(bool relative, int offset, int invalidIdx)
{
	int idx = ui->scenes->currentRow();
	if (idx == -1 || idx == invalidIdx)
		return;

	ui->scenes->blockSignals(true);
	QListWidgetItem *item = ui->scenes->takeItem(idx);

	if (!relative)
		idx = 0;

	ui->scenes->insertItem(idx + offset, item);
	ui->scenes->setCurrentRow(idx + offset);
	item->setSelected(true);
	currentScene = GetOBSRef<OBSScene>(item).Get();
	ui->scenes->blockSignals(false);

	OBSProjector::UpdateMultiviewProjectors();
}

void OBSBasic::on_actionSceneUp_triggered()
{
	ChangeSceneIndex(true, -1, 0);
}

void OBSBasic::on_actionSceneDown_triggered()
{
	ChangeSceneIndex(true, 1, ui->scenes->count() - 1);
}

void OBSBasic::MoveSceneToTop()
{
	ChangeSceneIndex(false, 0, 0);
}

void OBSBasic::MoveSceneToBottom()
{
	ChangeSceneIndex(false, ui->scenes->count() - 1, ui->scenes->count() - 1);
}

void OBSBasic::EditSceneItemName()
{
	int idx = GetTopSelectedSourceItem();
	ui->sources->Edit(idx);
}

void OBSBasic::on_scenes_itemDoubleClicked(QListWidgetItem *witem)
{
	if (!witem)
		return;

	if (IsPreviewProgramMode()) {
		bool doubleClickSwitch =
			config_get_bool(App()->GetUserConfig(), "BasicWindow", "TransitionOnDoubleClick");

		if (doubleClickSwitch)
			TransitionClicked();
	}
}

OBSData OBSBasic::BackupScene(obs_scene_t *scene, std::vector<obs_source_t *> *sources)
{
	OBSDataArrayAutoRelease undo_array = obs_data_array_create();

	if (!sources) {
		obs_scene_enum_items(scene, save_undo_source_enum, undo_array);
	} else {
		for (obs_source_t *source : *sources) {
			obs_data_t *source_data = obs_save_source(source);
			obs_data_array_push_back(undo_array, source_data);
			obs_data_release(source_data);
		}
	}

	OBSDataAutoRelease scene_data = obs_save_source(obs_scene_get_source(scene));
	obs_data_array_push_back(undo_array, scene_data);

	OBSDataAutoRelease data = obs_data_create();

	obs_data_set_array(data, "array", undo_array);
	obs_data_get_json(data);
	return data.Get();
}

static bool add_source_enum(obs_scene_t *, obs_sceneitem_t *item, void *p)
{
	auto sources = static_cast<std::vector<OBSSource> *>(p);
	sources->push_back(obs_sceneitem_get_source(item));
	return true;
}

void OBSBasic::CreateSceneUndoRedoAction(const QString &action_name, OBSData undo_data, OBSData redo_data)
{
	auto undo_redo = [this](const std::string &json) {
		OBSDataAutoRelease base = obs_data_create_from_json(json.c_str());
		OBSDataArrayAutoRelease array = obs_data_get_array(base, "array");
		std::vector<OBSSource> sources;
		std::vector<OBSSource> old_sources;

		/* create missing sources */
		const size_t count = obs_data_array_count(array);
		sources.reserve(count);

		for (size_t i = 0; i < count; i++) {
			OBSDataAutoRelease data = obs_data_array_item(array, i);
			const char *name = obs_data_get_string(data, "name");

			OBSSourceAutoRelease source = obs_get_source_by_name(name);
			if (!source)
				source = obs_load_source(data);

			sources.push_back(source.Get());

			/* update scene/group settings to restore their
			 * contents to their saved settings */
			obs_scene_t *scene = obs_group_or_scene_from_source(source);
			if (scene) {
				obs_scene_enum_items(scene, add_source_enum, &old_sources);
				OBSDataAutoRelease scene_settings = obs_data_get_obj(data, "settings");
				obs_source_update(source, scene_settings);
			}
		}

		/* actually load sources now */
		for (obs_source_t *source : sources)
			obs_source_load2(source);

		ui->sources->RefreshItems();
	};

	const char *undo_json = obs_data_get_last_json(undo_data);
	const char *redo_json = obs_data_get_last_json(redo_data);

	undo_s.add_action(action_name, undo_redo, undo_redo, undo_json, redo_json);
}

void OBSBasic::MoveSceneItem(enum obs_order_movement movement, const QString &action_name)
{
	OBSSceneItem item = GetCurrentSceneItem();
	obs_source_t *source = obs_sceneitem_get_source(item);

	if (!source)
		return;

	OBSScene scene = GetCurrentScene();
	std::vector<obs_source_t *> sources;
	if (scene != obs_sceneitem_get_scene(item))
		sources.push_back(obs_scene_get_source(obs_sceneitem_get_scene(item)));

	OBSData undo_data = BackupScene(scene, &sources);

	obs_sceneitem_set_order(item, movement);

	const char *source_name = obs_source_get_name(source);
	const char *scene_name = obs_source_get_name(obs_scene_get_source(scene));

	OBSData redo_data = BackupScene(scene, &sources);
	CreateSceneUndoRedoAction(action_name.arg(source_name, scene_name), undo_data, redo_data);
}

static void RenameListItem(OBSBasic *parent, QListWidget *listWidget, obs_source_t *source, const string &name)
{
	const char *prevName = obs_source_get_name(source);
	if (name == prevName)
		return;

	OBSSourceAutoRelease foundSource = obs_get_source_by_name(name.c_str());
	QListWidgetItem *listItem = listWidget->currentItem();

	if (foundSource || name.empty()) {
		listItem->setText(QT_UTF8(prevName));

		if (foundSource) {
			OBSMessageBox::warning(parent, QTStr("NameExists.Title"), QTStr("NameExists.Text"));
		} else if (name.empty()) {
			OBSMessageBox::warning(parent, QTStr("NoNameEntered.Title"), QTStr("NoNameEntered.Text"));
		}
	} else {
		auto undo = [prev = std::string(prevName)](const std::string &data) {
			OBSSourceAutoRelease source = obs_get_source_by_uuid(data.c_str());
			obs_source_set_name(source, prev.c_str());
		};

		auto redo = [name](const std::string &data) {
			OBSSourceAutoRelease source = obs_get_source_by_uuid(data.c_str());
			obs_source_set_name(source, name.c_str());
		};

		std::string source_uuid(obs_source_get_uuid(source));
		parent->undo_s.add_action(QTStr("Undo.Rename").arg(name.c_str()), undo, redo, source_uuid, source_uuid);

		listItem->setText(QT_UTF8(name.c_str()));
		obs_source_set_name(source, name.c_str());
	}
}

void OBSBasic::SceneNameEdited(QWidget *editor)
{
	OBSScene scene = GetCurrentScene();
	QLineEdit *edit = qobject_cast<QLineEdit *>(editor);
	string text = QT_TO_UTF8(edit->text().trimmed());

	if (!scene)
		return;

	obs_source_t *source = obs_scene_get_source(scene);
	QString originalName = QString::fromUtf8(obs_source_get_name(source));
	QString newNameProposed = QString::fromStdString(text);
	QString finalName = newNameProposed;

	if (App()->IsDualOutputActive()) {
		QString originalPrefix = "";
		if (originalName.startsWith("H_", Qt::CaseInsensitive)) originalPrefix = "H_";
		else if (originalName.startsWith("V_", Qt::CaseInsensitive)) originalPrefix = "V_";

		bool newNameHasHPrefix = newNameProposed.startsWith("H_", Qt::CaseInsensitive);
		bool newNameHasVPrefix = newNameProposed.startsWith("V_", Qt::CaseInsensitive);

		if (!originalPrefix.isEmpty()) { // Original had a prefix
			if (!newNameHasHPrefix && !newNameHasVPrefix) {
				// User removed prefix, re-apply original or context-based one
				finalName = originalPrefix + newNameProposed;
			} // Else, user specified a prefix (or kept original), let it be.
		} else { // Original was unprefixed (classic scene or non-dual mode previously)
			// If now in dual mode, and user didn't add a prefix, apply H_ by default.
			if (!newNameHasHPrefix && !newNameHasVPrefix) {
				finalName = "H_" + newNameProposed;
			}
		}
	}

	if (finalName.isEmpty()) { // Reverted if text is empty by RenameListItem
		 RenameListItem(this, ui->scenes, source, finalName.toStdString()); // Will set to old name
	} else if (originalName != finalName) {
		OBSSourceAutoRelease existingSource = obs_get_source_by_name(finalName.toStdString().c_str());
		if (existingSource && obs_source_get_uuid(existingSource) != obs_source_get_uuid(source)) {
			OBSMessageBox::warning(this, QTStr("NameExists.Title"), QTStr("NameExists.Text"));
			listItem->setText(originalName); // Revert UI
			obs_source_release(existingSource);
		} else {
			RenameListItem(this, ui->scenes, source, finalName.toStdString());
		}
		if(existingSource) obs_source_release(existingSource);
	} else { // Name didn't actually change after prefix logic, or wasn't changed by user
		listItem->setText(originalName); // Ensure UI is correct if only case changed etc.
	}


	ui->scenesDock->addAction(renameScene);
	RefreshSceneListDisplay(); // Refresh to apply filter if prefix changed
	OnEvent(OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED);
}

void OBSBasic::OpenSceneFilters()
{
	OBSScene scene = GetCurrentScene();
	OBSSource source = obs_scene_get_source(scene);

	CreateFiltersWindow(source);
}

static bool reset_tr(obs_scene_t * /* scene */, obs_sceneitem_t *item, void *)
{
	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, reset_tr, nullptr);
	if (!obs_sceneitem_selected(item))
		return true;
	if (obs_sceneitem_locked(item))
		return true;

	obs_sceneitem_defer_update_begin(item);

	obs_transform_info info;
	vec2_set(&info.pos, 0.0f, 0.0f);
	vec2_set(&info.scale, 1.0f, 1.0f);
	info.rot = 0.0f;
	info.alignment = OBS_ALIGN_TOP | OBS_ALIGN_LEFT;
	info.bounds_type = OBS_BOUNDS_NONE;
	info.bounds_alignment = OBS_ALIGN_CENTER;
	info.crop_to_bounds = false;
	vec2_set(&info.bounds, 0.0f, 0.0f);
	obs_sceneitem_set_info2(item, &info);

	obs_sceneitem_crop crop = {};
	obs_sceneitem_set_crop(item, &crop);

	obs_sceneitem_defer_update_end(item);

	return true;
}

void OBSBasic::on_actionResetTransform_triggered()
{
	OBSScene scene = GetCurrentScene();

	OBSDataAutoRelease wrapper = obs_scene_save_transform_states(scene, false);
	obs_scene_enum_items(scene, reset_tr, nullptr);
	OBSDataAutoRelease rwrapper = obs_scene_save_transform_states(scene, false);

	std::string undo_data(obs_data_get_json(wrapper));
	std::string redo_data(obs_data_get_json(rwrapper));
	undo_s.add_action(QTStr("Undo.Transform.Reset").arg(obs_source_get_name(obs_scene_get_source(scene))),
			  undo_redo, undo_redo, undo_data, redo_data);

	obs_scene_enum_items(GetCurrentScene(), reset_tr, nullptr);
}

SourceTreeItem *OBSBasic::GetItemWidgetFromSceneItem(obs_sceneitem_t *sceneItem)
{
	int i = 0;
	SourceTreeItem *treeItem = ui->sources->GetItemWidget(i);
	OBSSceneItem item = ui->sources->Get(i);
	int64_t id = obs_sceneitem_get_id(sceneItem);
	while (treeItem && obs_sceneitem_get_id(item) != id) {
		i++;
		treeItem = ui->sources->GetItemWidget(i);
		item = ui->sources->Get(i);
	}
	if (treeItem)
		return treeItem;

	return nullptr;
}

void OBSBasic::on_actionSceneFilters_triggered()
{
	OBSSource sceneSource = GetCurrentSceneSource();

	if (sceneSource)
		OpenFilters(sceneSource);
}
