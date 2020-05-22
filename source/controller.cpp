//------------------------------------------------------------------------
// Flags       : clang-format SMTGSequencer

#include "controller.h"
#include "process.h"
#include "version.h"

#include "vstgui/lib/cdropsource.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cscrollview.h"
#include "vstgui/lib/iviewlistener.h"
#include "vstgui/standalone/include/helpers/preferences.h"
#include "vstgui/standalone/include/helpers/value.h"
#include "vstgui/standalone/include/ialertbox.h"
#include "vstgui/standalone/include/iasync.h"
#include "vstgui/standalone/include/icommondirectories.h"
#include "vstgui/uidescription/cstream.h"
#include "vstgui/uidescription/delegationcontroller.h"
#include "vstgui/uidescription/iuidescription.h"
#include "vstgui/uidescription/uiattributes.h"

#include <array>
#include <cassert>
#include <fstream>

//------------------------------------------------------------------------
namespace Steinberg {
namespace Vst {
namespace ProjectCreator {

using namespace VSTGUI;
using namespace VSTGUI::Standalone;

//------------------------------------------------------------------------
namespace {

#if WINDOWS
constexpr auto PlatformPathDelimiter = '\\';
constexpr auto EnvPathSeparator = ';';
constexpr auto CMakeExecutableName = "CMake.exe";
#else
constexpr auto PlatformPathDelimiter = '/';
constexpr auto EnvPathSeparator = ':';
constexpr auto CMakeExecutableName = "cmake";
#endif

//------------------------------------------------------------------------
constexpr auto CMakeWebPageURL = "https://cmake.org";
constexpr auto SteinbergSDKWebPageURL = "https://www.steinberg.net/en/company/developers.html";
constexpr auto GitHubSDKWebPageURL = "https://github.com/steinbergmedia/vst3sdk";
constexpr auto VST3SDKPortalPageURL = "https://developer.steinberg.help/display/VST";

//------------------------------------------------------------------------
constexpr auto valueIdWelcomeDownloadSDK = "Welcome Download SDK";
constexpr auto valueIdWelcomeLocateSDK = "Welcome Locate SDK";
constexpr auto valueIdWelcomeDownloadCMake = "Welcome Download CMake";
constexpr auto valueIdWelcomeLocateCMake = "Welcome Locate CMake";
constexpr auto valueIdValidVSTSDKPath = "Valid VST SDK Path";
constexpr auto valueIdValidCMakePath = "Valid CMake Path";

//------------------------------------------------------------------------
const std::initializer_list<IStringListValue::StringType> pluginTypeDisplayStrings = {
    "Audio Effect", "Instrument"};
const std::array<std::string, 2> pluginTypeStrings = {"Fx", "Instrument"};

//------------------------------------------------------------------------
void showSimpleAlert (const char* headline, const char* description)
{
	AlertBoxForWindowConfig config;
	config.headline = headline;
	config.description = description;
	config.defaultButton = "OK";
	config.window = IApplication::instance ().getWindows ().front ();
	IApplication::instance ().showAlertBoxForWindow (config);
}

//------------------------------------------------------------------------
size_t makeValidCppName (std::string& str, char replaceChar = '_')
{
	size_t replaced = 0;
	std::replace_if (str.begin (), str.end (),
	                 [&] (auto c) {
		                 auto legal = (c >= 0x30 && c < 0x3A) || (c >= 0x41 && c < 0x5B) ||
		                              (c >= 0x61 && c < 0x7B) || c == replaceChar;
		                 if (!legal)
			                 replaced++;
		                 return !legal;
	                 },
	                 replaceChar);
	return replaced;
}

//------------------------------------------------------------------------
void makeValidCppValueString (IValue& value)
{
	if (auto strValue = value.dynamicCast<IStringValue> ())
	{
		auto str = strValue->getString ().getString ();
		auto replaced = makeValidCppName (str);
		if (replaced)
		{
			value.beginEdit ();
			strValue->setString (UTF8String (std::move (str)));
			value.endEdit ();
		}
	}
}

//------------------------------------------------------------------------
void setPreferenceStringValue (Preferences& prefs, const UTF8String& key, const ValuePtr& value)
{
	if (!value)
		return;
	if (auto strValue = value->dynamicCast<IStringValue> ())
		prefs.set (key, strValue->getString ());
	else
		prefs.set (key, value->getConverter ().valueAsString (value->getValue ()));
}

//------------------------------------------------------------------------
UTF8String getValueString (IValue& value)
{
	if (auto strValue = value.dynamicCast<IStringValue> ())
		return strValue->getString ();
	return {};
}

//------------------------------------------------------------------------
UTF8String getModelValueString (VSTGUI::Standalone::UIDesc::ModelBindingCallbacksPtr model,
                                const UTF8String& key)
{
	if (auto value = model->getValue (key))
		return getValueString (*value.get ());
	return {};
}

//------------------------------------------------------------------------
class ValueListenerViewController : public DelegationController, public ValueListenerAdapter
{
public:
	ValueListenerViewController (IController* parent, ValuePtr value)
	: DelegationController (parent), value (value)
	{
		value->registerListener (this);
	}

	virtual ~ValueListenerViewController () noexcept { value->unregisterListener (this); }

	const ValuePtr& getValue () const { return value; }

private:
	ValuePtr value {nullptr};
};

//------------------------------------------------------------------------
class ScriptScrollViewController : public ValueListenerViewController,
                                   public ViewListenerAdapter,
                                   public IContextMenuController2
{
public:
	ScriptScrollViewController (IController* parent, ValuePtr value)
	: ValueListenerViewController (parent, value)
	{
	}

	CView* verifyView (CView* view, const UIAttributes& attributes,
	                   const IUIDescription* description) override
	{
		if (auto sv = dynamic_cast<CScrollView*> (view))
		{
			scrollView = sv;
			if (auto label = dynamic_cast<CMultiLineTextLabel*> (scrollView->getView (0)))
			{
				label->registerViewListener (this);
			}
		}
		return controller->verifyView (view, attributes, description);
	}

	void scrollToBottom ()
	{
		if (!scrollView)
			return;
		auto containerSize = scrollView->getContainerSize ();
		containerSize.top = containerSize.bottom - 10;
		scrollView->makeRectVisible (containerSize);
	}

	void onEndEdit (IValue&) override { scrollToBottom (); }

	void viewWillDelete (CView* view) override
	{
		if (auto label = dynamic_cast<CMultiLineTextLabel*> (view))
			label->unregisterViewListener (this);
	}

	void viewAttached (CView* view) override
	{
		if (auto label = dynamic_cast<CMultiLineTextLabel*> (view))
		{
			if (label->getAutoHeight ())
			{
				label->setAutoHeight (false);
				label->setAutoHeight (true);
			}
			label->unregisterViewListener (this);
			scrollToBottom ();
		}
	}

	void appendContextMenuItems (COptionMenu& contextMenu, CView* view,
	                             const CPoint& where) override
	{
		if (auto stringValue = getValue ()->dynamicCast<IStringValue> ())
		{
			if (stringValue->getString ().empty ())
				return;
			auto commandItem = new CCommandMenuItem ({"Copy text to clipboard"});
			commandItem->setActions ([&, stringValue] (CCommandMenuItem*) {
				auto frame = contextMenu.getFrame ();
				if (!frame)
					return;
				auto data = CDropSource::create (
				    stringValue->getString ().data (),
				    static_cast<uint32_t> (stringValue->getString ().length ()),
				    IDataPackage::Type::kText);
				frame->setClipboard (data);
			});
			contextMenu.addEntry (commandItem);
		}
	}

	CScrollView* scrollView {nullptr};
};

//------------------------------------------------------------------------
class DimmViewController : public ValueListenerViewController
{
public:
	DimmViewController (IController* parent, ValuePtr value, float dimm = 0.f)
	: ValueListenerViewController (parent, value), dimmValue (dimm)
	{
	}

	CView* verifyView (CView* view, const UIAttributes& attributes,
	                   const IUIDescription* description) override
	{
		if (auto name = attributes.getAttributeValue (IUIDescription::kCustomViewName))
		{
			if (*name == "Container")
			{
				dimmView = view;
				onEndEdit (*getValue ());
			}
		}
		return controller->verifyView (view, attributes, description);
	}

	void onEndEdit (IValue& value) override
	{
		if (!dimmView)
			return;
		bool b = value.getValue () > 0.5;
		float alphaValue = b ? dimmValue : 1.f;
		dimmView->setAlphaValue (alphaValue);
		dimmView->setMouseEnabled (!b);
	}

	float dimmValue {0.f};
	CView* dimmView {nullptr};
};

//------------------------------------------------------------------------
} // anonymous

//------------------------------------------------------------------------
void Controller::onSetContentView (IWindow& window, const VSTGUI::SharedPointer<CFrame>& view)
{
	contentView = view;
}

//------------------------------------------------------------------------
Controller::Controller ()
{
	Preferences prefs;
	auto vendorPref = prefs.get (valueIdVendorName);
	auto emailPref = prefs.get (valueIdVendorEMail);
	auto urlPref = prefs.get (valueIdVendorURL);
	auto namespacePref = prefs.get (valueIdVendorNamespace);
	auto vstSdkPathPref = prefs.get (valueIdVSTSDKPath);
	auto cmakePathPref = prefs.get (valueIdCMakePath);
	auto pluginPathPref = prefs.get (valueIdPluginPath);

	auto envPaths = getEnvPaths ();
	if (!cmakePathPref || cmakePathPref->empty ())
		cmakePathPref = findCMakePath (envPaths);

	model = UIDesc::ModelBindingCallbacks::make ();
	/* UI only */
	model->addValue (Value::makeStringValue (valueIdAppVersion, BUILD_STRING));

	model->addValue (Value::makeStringListValue (
	    valueIdTabBar, {"Welcome", "Create Plug-In Project", "Preferences"}));

	model->addValue (Value::make (valueIdCreateProject),
	                 UIDesc::ValueCalls::onAction ([this] (IValue& v) {
		                 createProject ();
		                 v.performEdit (0.);
	                 }));

	model->addValue (Value::makeStringValue (valueIdScriptOutput, ""),
	                 UIDesc::ValueCalls::onEndEdit ([this] (IValue& v) { onScriptOutput (); }));
	model->addValue (Value::make (valueIdScriptRunning),
	                 UIDesc::ValueCalls::onEndEdit ([this] (IValue& v) {
		                 onScriptRunning (v.getValue () > 0.5 ? true : false);
	                 }));

	model->addValue (Value::make (valueIdCopyScriptOutput),
	                 UIDesc::ValueCalls::onAction ([this] (IValue& v) {
		                 copyScriptOutputToClipboard ();
		                 v.performEdit (0.);
	                 }));

	/* Factory/Vendor Infos */
	model->addValue (Value::makeStringValue (valueIdVendorName, vendorPref ? *vendorPref : ""),
	                 UIDesc::ValueCalls::onEndEdit ([this] (IValue&) { storePreferences (); }));
	model->addValue (Value::makeStringValue (valueIdVendorEMail, emailPref ? *emailPref : ""),
	                 UIDesc::ValueCalls::onEndEdit ([this] (IValue&) { storePreferences (); }));
	model->addValue (Value::makeStringValue (valueIdVendorURL, urlPref ? *urlPref : ""),
	                 UIDesc::ValueCalls::onEndEdit ([this] (IValue&) { storePreferences (); }));
	model->addValue (
	    Value::makeStringValue (valueIdVendorNamespace, namespacePref ? *namespacePref : ""),
	    UIDesc::ValueCalls::onEndEdit ([this] (IValue& val) {
		    makeValidCppValueString (val);
		    storePreferences ();
	    }));

	/* Directories */
	model->addValue (Value::make (valueIdChooseVSTSDKPath),
	                 UIDesc::ValueCalls::onAction ([this] (IValue& v) {
		                 chooseVSTSDKPath ();
		                 v.performEdit (0.);
	                 }));
	model->addValue (Value::make (valueIdChooseCMakePath),
	                 UIDesc::ValueCalls::onAction ([this] (IValue& v) {
		                 chooseCMakePath ();
		                 v.performEdit (0.);
	                 }));
	model->addValue (
	    Value::makeStringValue (valueIdVSTSDKPath, vstSdkPathPref ? *vstSdkPathPref : ""),
	    UIDesc::ValueCalls::onEndEdit ([this] (IValue&) { storePreferences (); }));
	model->addValue (Value::makeStringValue (valueIdCMakePath, cmakePathPref ? *cmakePathPref : ""),
	                 UIDesc::ValueCalls::onEndEdit ([this] (IValue&) { storePreferences (); }));

	/* Plug-In */
	model->addValue (Value::makeStringValue (valueIdPluginName, ""));
	model->addValue (Value::makeStringListValue (valueIdPluginType, pluginTypeDisplayStrings));
	model->addValue (Value::makeStringValue (valueIdPluginBundleID, ""));
	model->addValue (Value::makeStringValue (valueIdPluginFilenamePrefix, ""));
	model->addValue (
	    Value::makeStringValue (valueIdPluginClassName, ""),
	    UIDesc::ValueCalls::onEndEdit ([] (IValue& val) { makeValidCppValueString (val); }));
	model->addValue (
	    Value::makeStringValue (valueIdPluginPath, pluginPathPref ? *pluginPathPref : ""),
	    UIDesc::ValueCalls::onEndEdit ([this] (IValue&) { storePreferences (); }));

	model->addValue (Value::make (valueIdChoosePluginPath),
	                 UIDesc::ValueCalls::onAction ([this] (IValue& v) {
		                 choosePluginPath ();
		                 v.performEdit (0.);
	                 }));

	/* CMake */
	model->addValue (Value::makeStringListValue (valueIdCMakeGenerators, {"", ""}),
	                 UIDesc::ValueCalls::onEndEdit ([this] (IValue&) { storePreferences (); }));

	/* Welcome Page */
	model->addValue (Value::make (valueIdWelcomeDownloadSDK),
	                 UIDesc::ValueCalls::onAction ([this] (IValue& v) {
		                 downloadVSTSDK ();
		                 v.performEdit (0.);
	                 }));
	model->addValue (Value::make (valueIdWelcomeLocateSDK),
	                 UIDesc::ValueCalls::onAction ([this] (IValue& v) {
		                 chooseVSTSDKPath ();
		                 v.performEdit (0.);
	                 }));
	model->addValue (Value::make (valueIdWelcomeDownloadCMake),
	                 UIDesc::ValueCalls::onAction ([this] (IValue& v) {
		                 downloadCMake ();
		                 v.performEdit (0.);
	                 }));
	model->addValue (Value::make (valueIdWelcomeLocateCMake),
	                 UIDesc::ValueCalls::onAction ([this] (IValue& v) {
		                 chooseCMakePath ();
		                 verifyCMakeInstallation ();
		                 v.performEdit (0.);
	                 }));

	/* Valid Path values */
	model->addValue (Value::make (valueIdValidVSTSDKPath));
	model->addValue (Value::make (valueIdValidCMakePath));

	// sub controllers
	addCreateViewControllerFunc (
	    "ScriptOutputController",
	    [this] (const auto& name, auto parent, const auto uiDesc) -> IController* {
		    return new ScriptScrollViewController (parent, model->getValue (valueIdScriptOutput));
	    });
	addCreateViewControllerFunc (
	    "DimmViewController_CMake",
	    [this] (const auto& name, auto parent, const auto uiDesc) -> IController* {
		    return new DimmViewController (parent, model->getValue (valueIdValidCMakePath));
	    });
	addCreateViewControllerFunc (
	    "DimmViewController_VSTSDK",
	    [this] (const auto& name, auto parent, const auto uiDesc) -> IController* {
		    return new DimmViewController (parent, model->getValue (valueIdValidVSTSDKPath));
	    });
	addCreateViewControllerFunc (
	    "DimmViewController_CreateProjectTab",
	    [this] (const auto& name, auto parent, const auto uiDesc) -> IController* {
		    return new DimmViewController (parent, model->getValue (valueIdScriptRunning), 0.5f);
	    });
}

//------------------------------------------------------------------------
void Controller::storePreferences ()
{
	Preferences prefs;
	setPreferenceStringValue (prefs, valueIdVendorName, model->getValue (valueIdVendorName));
	setPreferenceStringValue (prefs, valueIdVendorEMail, model->getValue (valueIdVendorEMail));
	setPreferenceStringValue (prefs, valueIdVendorURL, model->getValue (valueIdVendorURL));
	setPreferenceStringValue (prefs, valueIdVendorNamespace,
	                          model->getValue (valueIdVendorNamespace));
	setPreferenceStringValue (prefs, valueIdVSTSDKPath, model->getValue (valueIdVSTSDKPath));
	setPreferenceStringValue (prefs, valueIdCMakePath, model->getValue (valueIdCMakePath));
	setPreferenceStringValue (prefs, valueIdPluginPath, model->getValue (valueIdPluginPath));
	setPreferenceStringValue (prefs, valueIdCMakeGenerators,
	                          model->getValue (valueIdCMakeGenerators));
}

//------------------------------------------------------------------------
void Controller::onScriptRunning (bool state)
{
	static constexpr auto valuesToDisable = {
	    valueIdTabBar,
	    valueIdVendorName,
	    valueIdVendorEMail,
	    valueIdVendorURL,
	    valueIdVendorNamespace,
	    valueIdVSTSDKPath,
	    valueIdCMakePath,
	    valueIdPluginType,
	    valueIdPluginPath,
	    valueIdPluginName,
	    valueIdPluginClassName,
	    valueIdPluginBundleID,
	    valueIdPluginFilenamePrefix,
	    valueIdChooseCMakePath,
	    valueIdChooseVSTSDKPath,
	    valueIdChoosePluginPath,
	    valueIdCreateProject,
	    valueIdCMakeGenerators,
	};
	for (const auto& valueID : valuesToDisable)
	{
		if (auto value = model->getValue (valueID))
			value->setActive (!state);
	}
}

//------------------------------------------------------------------------
void Controller::onShow (const IWindow& window)
{
	bool sdkInstallationVerified = verifySDKInstallation ();
	bool cmakeInstallationVerified = verifyCMakeInstallation ();
	Value::performSinglePlainEdit (*model->getValue (valueIdTabBar),
	                               sdkInstallationVerified && cmakeInstallationVerified ? 1 : 0);

	if (cmakeInstallationVerified)
		gatherCMakeInformation ();
}

//------------------------------------------------------------------------
void Controller::gatherCMakeInformation ()
{
	auto cmakePathStr = getModelValueString (model, valueIdCMakePath);
	if (auto process = Process::create (cmakePathStr.getString ()))
	{
		Process::ArgumentList args;
		args.add ("-E");
		args.add ("capabilities");

		auto scriptRunningValue = model->getValue (valueIdScriptRunning);
		assert (scriptRunningValue);
		Value::performSingleEdit (*scriptRunningValue, 1.);
		auto outputString = std::make_shared<std::string> ();
		auto result = process->run (args, [this, scriptRunningValue, outputString,
		                                   process] (Process::CallbackParams& p) mutable {
			if (!p.buffer.empty ())
			{
				*outputString += std::string (p.buffer.data (), p.buffer.size ());
			}
			if (p.isEOF)
			{
				if (auto capabilities = parseCMakeCapabilities (*outputString))
				{
					auto cmakeGeneratorsValue = model->getValue (valueIdCMakeGenerators);
					assert (cmakeGeneratorsValue);
					cmakeGeneratorsValue->dynamicCast<IStringListValue> ()->updateStringList (
					    capabilities->generators);

					Preferences prefs;
					if (auto generatorPref = prefs.get (valueIdCMakeGenerators))
					{
						auto value =
						    cmakeGeneratorsValue->getConverter ().stringAsValue (*generatorPref);
						cmakeGeneratorsValue->performEdit (value);
					}
					else
					{
						// we should use some defaults here
					}
					cmakeCapabilities = std::move (*capabilities);
				}
				else
				{
					// TODO: show error?
				}
				Value::performSingleEdit (*scriptRunningValue, 0.);
				process.reset ();
			}
		});
		if (!result)
		{
			// TODO: show error!
		}
	}
}

//------------------------------------------------------------------------
template <typename Proc>
void Controller::runFileSelector (const UTF8String& valueId, CNewFileSelector::Style style,
                                  Proc proc) const
{
	auto value = model->getValue (valueId);
	if (!value)
		return;

	auto fileSelector = owned (CNewFileSelector::create (contentView, style));
	if (!fileSelector)
		return;

	Preferences prefs;
	if (auto pathPref = prefs.get (valueId))
		fileSelector->setInitialDirectory (*pathPref);

	fileSelector->run ([proc, value] (CNewFileSelector* fs) {
		if (fs->getNumSelectedFiles () == 0)
			return;
		if (proc (fs->getSelectedFile (0)))
			Value::performStringValueEdit (*value, fs->getSelectedFile (0));
	});
}

//------------------------------------------------------------------------
void Controller::chooseVSTSDKPath ()
{
	runFileSelector (
	    valueIdVSTSDKPath, CNewFileSelector::kSelectDirectory, [this] (const UTF8String& path) {
		    if (!validateVSTSDKPath (path))
		    {
			    showSimpleAlert (
			        "Wrong VST SDK path!",
			        "The selected folder does not look like the root folder of the VST SDK.");
			    return false;
		    }
		    Async::schedule (Async::mainQueue (), [this] () { verifySDKInstallation (); });
		    return true;
	    });
}

//------------------------------------------------------------------------
void Controller::chooseCMakePath ()
{
	runFileSelector (
	    valueIdCMakePath, CNewFileSelector::kSelectFile, [this] (const UTF8String& path) {
		    if (!validateCMakePath (path))
		    {
			    showSimpleAlert ("Wrong CMake path!", "The selected file is not cmake.");
			    return false;
		    }
		    Async::schedule (Async::mainQueue (), [this] () {
			    verifyCMakeInstallation ();
			    gatherCMakeInformation ();
		    });
		    return true;
	    });
}

//------------------------------------------------------------------------
void Controller::choosePluginPath ()
{
	runFileSelector (valueIdPluginPath, CNewFileSelector::kSelectDirectory,
	                 [this] (const UTF8String& path) { return validatePluginPath (path); });
}

//------------------------------------------------------------------------
void Controller::downloadVSTSDK ()
{
	AlertBoxForWindowConfig alert;
	alert.window = IApplication::instance ().getWindows ().front ();
	alert.headline = "Which SDK to download?";
	alert.description = "TODO: description";
	alert.defaultButton = "Commercial";
	alert.secondButton = "Open Source";
	alert.thirdButton = "Cancel";
	alert.callback = [] (AlertResult result) {
		switch (result)
		{
			case AlertResult::DefaultButton:
			{
				openURL (SteinbergSDKWebPageURL);
				break;
			}
			case AlertResult::SecondButton:
			{
				openURL (GitHubSDKWebPageURL);
				break;
			}
			case AlertResult::ThirdButton:
			{
				// Canceled
				break;
			}
			default:
			{
				assert (false);
				break;
			}
		}
	};
	IApplication::instance ().showAlertBoxForWindow (alert);
}

//------------------------------------------------------------------------
void Controller::downloadCMake ()
{
	openURL (CMakeWebPageURL);
}

//------------------------------------------------------------------------
bool Controller::verifySDKInstallation ()
{
	auto sdkPathStr = getModelValueString (model, valueIdVSTSDKPath);
	auto result = !(sdkPathStr.empty () || !validateVSTSDKPath (sdkPathStr));
	Value::performSinglePlainEdit (*model->getValue (valueIdValidVSTSDKPath), result);
	return result;
}

//------------------------------------------------------------------------
bool Controller::verifyCMakeInstallation ()
{
	auto cmakePathStr = getModelValueString (model, valueIdCMakePath);
	auto result = !(cmakePathStr.empty () || !validateCMakePath (cmakePathStr));
	Value::performSinglePlainEdit (*model->getValue (valueIdValidCMakePath), result);
	return result;
}

//------------------------------------------------------------------------
void Controller::showCMakeNotInstalledWarning ()
{
	AlertBoxForWindowConfig config;
	config.headline = "CMake not found!";
	config.description = "You need to install CMake for your platform to use this application.";
	config.defaultButton = "OK";
	config.secondButton = "Download CMake";
	config.window = IApplication::instance ().getWindows ().front ();
	config.callback = [this] (AlertResult result) {
		if (result == AlertResult::SecondButton)
			downloadCMake ();
	};
	IApplication::instance ().showAlertBoxForWindow (config);
}

//------------------------------------------------------------------------
bool Controller::validateVSTSDKPath (const UTF8String& path)
{
	auto p = path.getString ();
	if (*p.rbegin () != PlatformPathDelimiter)
		p += PlatformPathDelimiter;
	p += "pluginterfaces";
	p += PlatformPathDelimiter;
	p += "vst";
	p += PlatformPathDelimiter;
	p += "vsttypes.h";
	std::ifstream stream (p);
	return stream.is_open ();
}

//------------------------------------------------------------------------
bool Controller::validateCMakePath (const UTF8String& path)
{
	std::ifstream stream (path.getString ());
	return stream.is_open ();
}

//------------------------------------------------------------------------
bool Controller::validatePluginPath (const UTF8String& path)
{
	// TODO: check that the path is valid
	return true;
}

//------------------------------------------------------------------------
void Controller::createProject ()
{
	if (cmakeCapabilities.versionMajor == 0)
	{
		showCMakeNotInstalledWarning ();
		return;
	}
	auto _sdkPathStr = getModelValueString (model, valueIdVSTSDKPath);
	auto cmakePathStr = getModelValueString (model, valueIdCMakePath).getString ();
	auto pluginOutputPathStr = getModelValueString (model, valueIdPluginPath).getString ();
	auto vendorStr = getModelValueString (model, valueIdVendorName).getString ();
	auto vendorHomePageStr = getModelValueString (model, valueIdVendorURL).getString ();
	auto emailStr = getModelValueString (model, valueIdVendorEMail).getString ();
	auto pluginNameStr = getModelValueString (model, valueIdPluginName).getString ();
	auto filenamePrefixStr = getModelValueString (model, valueIdPluginFilenamePrefix).getString ();
	auto pluginBundleIDStr = getModelValueString (model, valueIdPluginBundleID).getString ();
	auto vendorNamspaceStr = getModelValueString (model, valueIdVendorNamespace).getString ();
	auto pluginClassNameStr = getModelValueString (model, valueIdPluginClassName).getString ();
	auto pluginTypeValue = model->getValue (valueIdPluginType);
	assert (pluginTypeValue);
	auto pluginTypeIndex = static_cast<size_t> (
	    pluginTypeValue->getConverter ().normalizedToPlain (pluginTypeValue->getValue ()));
	auto pluginTypeStr = pluginTypeStrings[pluginTypeIndex];

	if (_sdkPathStr.empty () || !validateVSTSDKPath (_sdkPathStr))
	{
		showSimpleAlert ("Cannot create Project", "The VST3 SDK Path is not correct.");
		return;
	}
	auto sdkPathStr = _sdkPathStr.getString ();
	unixfyPath (sdkPathStr);
	if (pluginOutputPathStr.empty ())
	{
		showSimpleAlert ("Cannot create Project", "You need to specify an output directory.");
		return;
	}
	if (pluginNameStr.empty ())
	{
		showSimpleAlert ("Cannot create Project", "You need to specify a name for your plugin.");
		return;
	}
	if (pluginBundleIDStr.empty ())
	{
		showSimpleAlert ("Cannot create Project", "You need to specify a bundle ID.");
		return;
	}

	if (pluginClassNameStr.empty ())
	{
		pluginClassNameStr = pluginNameStr;
		makeValidCppName (pluginClassNameStr);
	}
	auto cmakeProjectName = pluginNameStr;
	makeValidCppName (cmakeProjectName);

	if (auto scriptPath = IApplication::instance ().getCommonDirectories ().get (
	        CommonDirectoryLocation::AppResourcesPath))
	{
		*scriptPath += "GenerateVST3Plugin.cmake";

		Process::ArgumentList args;
		args.add ("-DSMTG_VST3_SDK_SOURCE_DIR_CLI=\"" + sdkPathStr + "\"");
		args.add ("-DSMTG_GENERATOR_OUTPUT_DIRECTORY_CLI=\"" + pluginOutputPathStr + "\"");
		args.add ("-DSMTG_PLUGIN_NAME_CLI=\"" + pluginNameStr + "\"");
		args.add ("-DSMTG_PLUGIN_CATEGORY_CLI=\"" + pluginTypeStr + "\"");
		args.add ("-DSMTG_CMAKE_PROJECT_NAME_CLI=\"" + cmakeProjectName + "\"");
		args.add ("-DSMTG_PLUGIN_BUNDLE_NAME_CLI=\"" + pluginNameStr + "\"");
		args.add ("-DSMTG_PLUGIN_IDENTIFIER_CLI=\"" + pluginBundleIDStr + "\"");
		args.add ("-DSMTG_VENDOR_NAME_CLI=\"" + vendorStr + "\"");
		args.add ("-DSMTG_VENDOR_HOMEPAGE_CLI=\"" + vendorHomePageStr + "\"");
		args.add ("-DSMTG_VENDOR_EMAIL_CLI=\"" + emailStr + "\"");
		args.add ("-DSMTG_PREFIX_FOR_FILENAMES_CLI=\"" + filenamePrefixStr + "\"");
		if (!vendorNamspaceStr.empty ())
			args.add ("-DSMTG_VENDOR_NAMESPACE_CLI=\"" + vendorNamspaceStr + "\"");
		if (!pluginClassNameStr.empty ())
			args.add ("-DSMTG_PLUGIN_CLASS_NAME_CLI=\"" + pluginClassNameStr + "\"");

		// DSMTG_PLUGIN_BUNDLE_NAME_CLI
		args.add ("-P");
		args.add (scriptPath->getString ());

		if (auto process = Process::create (cmakePathStr))
		{
			auto scriptRunningValue = model->getValue (valueIdScriptRunning);
			assert (scriptRunningValue);
			Value::performSingleEdit (*scriptRunningValue, 1.);
			auto scriptOutputValue = model->getValue (valueIdScriptOutput);
			assert (scriptOutputValue);

			Value::performStringValueEdit (*scriptOutputValue, cmakePathStr.data ());
			Value::performStringAppendValueEdit (*scriptOutputValue, " " + *scriptPath);
			for (const auto& arg : args.args)
			{
				Value::performStringAppendValueEdit (*scriptOutputValue, " " + arg);
			}

			auto projectPath = pluginOutputPathStr + PlatformPathDelimiter + pluginNameStr;
			if (!process->run (args, [this, scriptRunningValue, scriptOutputValue, process,
			                          projectPath] (Process::CallbackParams& p) mutable {
				    if (!p.buffer.empty ())
				    {
					    Value::performStringAppendValueEdit (
					        *scriptOutputValue, std::string (p.buffer.data (), p.buffer.size ()));
				    }
				    if (p.isEOF)
				    {
					    assert (scriptRunningValue);
					    Value::performSingleEdit (*scriptRunningValue, 0.);
					    if (p.resultCode == 0)
						    runProjectCMake (projectPath);
					    process.reset ();
				    }
			    }))
			{
				showSimpleAlert ("Could not execute CMake", "Please verify your path to CMake!");
				assert (scriptRunningValue);
				Value::performSingleEdit (*scriptRunningValue, 0.);
			}
		}
	}
}

//------------------------------------------------------------------------
void Controller::runProjectCMake (const std::string& path)
{
	auto cmakePathStr = getModelValueString (model, valueIdCMakePath);
	auto value = model->getValue (valueIdCMakeGenerators);
	assert (value);
	if (!value)
		return;
	auto generator = value->getConverter ().valueAsString (value->getValue ());
	if (generator.getString ().find (' ') != std::string::npos)
	{
		generator = "\"" + generator + "\"";
	}
	if (auto process = Process::create (cmakePathStr.getString ()))
	{
		auto scriptRunningValue = model->getValue (valueIdScriptRunning);
		assert (scriptRunningValue);
		Value::performSingleEdit (*scriptRunningValue, 1.);
		auto scriptOutputValue = model->getValue (valueIdScriptOutput);

		Process::ArgumentList args;
		args.add ("-G" + generator.getString ());
		args.add ("-S");
		args.addPath (path);
		args.add ("-B");
		auto buildDir = path;
		buildDir += PlatformPathDelimiter;
		buildDir += "build";
		args.addPath (buildDir);

		Value::performStringAppendValueEdit (*scriptOutputValue, "\n" + cmakePathStr + " ");
		for (const auto& a : args.args)
			Value::performStringAppendValueEdit (*scriptOutputValue, UTF8String (a) + " ");
		Value::performStringAppendValueEdit (*scriptOutputValue, "\n");

		auto result = process->run (args, [this, scriptRunningValue, scriptOutputValue, buildDir,
		                                   process] (Process::CallbackParams& p) mutable {
			if (!p.buffer.empty ())
			{
				Value::performStringAppendValueEdit (
				    *scriptOutputValue, std::string (p.buffer.data (), p.buffer.size ()));
			}
			if (p.isEOF)
			{
				assert (scriptRunningValue);
				Value::performSingleEdit (*scriptRunningValue, 0.);
				if (p.resultCode == 0)
					openCMakeGeneratedProject (buildDir);
				process.reset ();
			}
		});
		if (!result)
		{
			// TODO: Show error
		}
	}
}

//------------------------------------------------------------------------
void Controller::openCMakeGeneratedProject (const std::string& path)
{
	auto cmakePathStr = getModelValueString (model, valueIdCMakePath);
	if (auto process = Process::create (cmakePathStr.getString ()))
	{
		auto scriptOutputValue = model->getValue (valueIdScriptOutput);
		Process::ArgumentList args;
		args.add ("--open");
		args.addPath (path);
		auto result = process->run (
		    args, [this, scriptOutputValue, process] (Process::CallbackParams& p) mutable {
			    if (!p.buffer.empty ())
			    {
				    Value::performStringAppendValueEdit (
				        *scriptOutputValue, std::string (p.buffer.data (), p.buffer.size ()));
			    }
			    if (p.isEOF)
			    {
				    process.reset ();
			    }
		    });
		if (!result)
		{
			// TODO: Show error
		}
	}
}

//------------------------------------------------------------------------
void Controller::onScriptOutput ()
{
}

//------------------------------------------------------------------------
void Controller::copyScriptOutputToClipboard ()
{
	if (auto value = model->getValue (valueIdScriptOutput))
	{
		if (auto stringValue = value->dynamicCast<IStringValue> ())
		{
			if (stringValue->getString ().empty ())
				return;
			auto frame = contentView.get ();
			if (!frame)
				return;
			auto data =
			    CDropSource::create (stringValue->getString ().data (),
			                         static_cast<uint32_t> (stringValue->getString ().length ()),
			                         IDataPackage::Type::kText);
			frame->setClipboard (data);
		}
	}
}

//------------------------------------------------------------------------
auto Controller::getEnvPaths () -> StringList
{
	StringList result;
	if (auto envPath = std::getenv ("PATH"))
	{
		std::istringstream input;
		input.str (envPath);
		std::string el;
		while (std::getline (input, el, EnvPathSeparator))
		{
			if (*el.rbegin () != PlatformPathDelimiter)
				el += PlatformPathDelimiter;
			result.emplace_back (std::move (el));
		}
	}
	return result;
}

//------------------------------------------------------------------------
VSTGUI::Optional<UTF8String> Controller::findCMakePath (const StringList& envPaths)
{
	if (!envPaths.empty ())
	{
		for (auto path : envPaths)
		{
			path += CMakeExecutableName;
			std::ifstream stream (path);
			if (stream.is_open ())
			{
				return {std::move (path)};
			}
		}
	}
#if !WINDOWS
	std::string path = "/usr/local/bin/cmake";
	std::ifstream stream (path);
	if (stream.is_open ())
		return {std::move (path)};
#endif
	return {};
}

//------------------------------------------------------------------------
const IMenuBuilder* Controller::getWindowMenuBuilder (const IWindow& window) const
{
	return this;
}

//------------------------------------------------------------------------
} // ProjectCreator
} // Vst
} // Steinberg
