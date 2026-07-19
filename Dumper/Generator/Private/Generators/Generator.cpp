#include "Generators/Generator.h"
#include "Managers/StructManager.h"
#include "Managers/EnumManager.h"
#include "Managers/MemberManager.h"
#include "Managers/PackageManager.h"

#include "HashStringTable.h"
#include "Utils.h"

#include "Platform.h"
#include "Json/json.hpp"

#include <fstream>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

inline void InitSettings()
{
    Settings::InitWeakObjectPtrSettings();
    Settings::InitLargeWorldCoordinateSettings();

    Settings::InitObjectPtrPropertySettings();
    Settings::InitArrayDimSizeSettings();
}

void Generator::InitEngineCore()
{
    ObjectArray::Init(0x0000000, 0x10000, FChunkedFixedUObjectArrayLayout{}, nullptr);
    
    ObjectArray::Init();
    CALL_PLATFORM_SPECIFIC_FUNCTION(FName::Init);
    Off::Init();
    PropertySizes::Init();
    CALL_PLATFORM_SPECIFIC_FUNCTION(Off::InSDK::ProcessEvent::InitPE); 
    Off::InSDK::World::InitGWorld(); 
    Off::InSDK::Text::InitTextOffsets(); 
    InitSettings();
}

void Generator::InitInternal()
{
    PackageManager::Init();
    StructManager::Init();
    EnumManager::Init();
    MemberManager::Init();
    PackageManager::PostInit();
}

std::string ResolveHeaderPath(std::string IncludePath, std::string ModuleRelPath, const std::string& PackageName, const fs::path& DumperFolder)
{
    std::string TargetPath = IncludePath.empty() ? ModuleRelPath : IncludePath;

    if (TargetPath.empty()) return "";

    if (TargetPath.starts_with("Classes/")) TargetPath = TargetPath.substr(8);
    else if (TargetPath.starts_with("Public/")) TargetPath = TargetPath.substr(7);
    else if (TargetPath.starts_with("Private/")) TargetPath = TargetPath.substr(8);

    fs::path Absolute = DumperFolder / "Source" / PackageName / TargetPath;
    
    return fs::absolute(Absolute).generic_string();
}

bool Generator::SetupDumperFolder()
{
    try
    {
       std::string FolderName = (Settings::Generator::GameVersion + '-' + Settings::Generator::GameName);
       FileNameHelper::MakeValidFileName(FolderName);
       DumperFolder = fs::path(Settings::Generator::SDKGenerationPath) / FolderName;

       if (fs::exists(DumperFolder))
       {
          fs::path Old = DumperFolder.generic_string() + "_OLD";
          fs::remove_all(Old);
          fs::rename(DumperFolder, Old);
       }
       fs::create_directories(DumperFolder);
    }
    catch (const std::filesystem::filesystem_error& fe)
    {
       std::cerr << "Could not create required folders! Info: \n";
       std::cerr << fe.what() << std::endl;
       return false;
    }
    return true;
}

bool Generator::SetupFolders(std::string& FolderName, fs::path& OutFolder)
{
    fs::path Dummy;
    std::string EmptyName = "";
    return SetupFolders(FolderName, OutFolder, EmptyName, Dummy);
}

bool Generator::SetupFolders(std::string& FolderName, fs::path& OutFolder, std::string& SubfolderName, fs::path& OutSubFolder)
{
    FileNameHelper::MakeValidFileName(FolderName);
    FileNameHelper::MakeValidFileName(SubfolderName);

    try
    {
       OutFolder = DumperFolder / FolderName;
       OutSubFolder = OutFolder / SubfolderName;
             
       if (fs::exists(OutFolder))
       {
          fs::path Old = OutFolder.generic_string() + "_OLD";
          fs::remove_all(Old);
          fs::rename(OutFolder, Old);
       }
       fs::create_directories(OutFolder);

       if (!SubfolderName.empty())
          fs::create_directories(OutSubFolder);
    }
    catch (const std::filesystem::filesystem_error& fe)
    {
       std::cerr << "Could not create required folders! Info: \n";
       std::cerr << fe.what() << std::endl;
       return false;
    }
    return true;
}

template <typename K, typename V>
bool IsSafeMap(const UC::TMap<K, V>* Map) 
{
    if (!Map) return false;
    uintptr_t DataPtr = *reinterpret_cast<const uintptr_t*>(Map);
    int32 Num = reinterpret_cast<const int32*>(Map)[2]; 
    int32 Max = reinterpret_cast<const int32*>(Map)[3]; 
    
    if (Num < 0 || Max < 0 || Num > Max || Max > 0x100000) return false;
    if (Max > 0 && DataPtr < 0x10000) return false;
    return true;
}

struct FWeakObjectPtr {
    int32 ObjectIndex;
    int32 ObjectSerialNumber;
};

int32 FindClassFlagsOffset()
{
    UEClass ObjClass = ObjectArray::FindClassFast("Object");
    if (!ObjClass) return 0x0;

    uint8* Base = static_cast<uint8*>(ObjClass.GetAddress());
    
    for (int32 i = 0x40; i < 0x150; i += 4)
    {
        EClassFlags Flags = *reinterpret_cast<EClassFlags*>(Base + i);
        if ((static_cast<uint32>(Flags) & 0x10000081) == 0x10000081)
        {
            return i;
        }
    }
    return 0x0;
}

std::vector<std::string> GetClassFlagsArray(EClassFlags Flags)
{
    std::vector<std::string> Result;
    uint32 F = static_cast<uint32>(Flags);
    
    if (F & static_cast<uint32>(EClassFlags::Abstract)) Result.push_back("Abstract");
    if (F & static_cast<uint32>(EClassFlags::DefaultConfig)) Result.push_back("DefaultConfig");
    if (F & static_cast<uint32>(EClassFlags::Config)) Result.push_back("Config");
    if (F & static_cast<uint32>(EClassFlags::Transient)) Result.push_back("Transient");
    if (F & static_cast<uint32>(EClassFlags::Parsed)) Result.push_back("Parsed");
    if (F & static_cast<uint32>(EClassFlags::MatchedSerializers)) Result.push_back("MatchedSerializers");
    if (F & static_cast<uint32>(EClassFlags::ProjectUserConfig)) Result.push_back("ProjectUserConfig");
    if (F & static_cast<uint32>(EClassFlags::Native)) Result.push_back("Native");
    if (F & static_cast<uint32>(EClassFlags::NoExport)) Result.push_back("NoExport");
    if (F & static_cast<uint32>(EClassFlags::NotPlaceable)) Result.push_back("NotPlaceable");
    if (F & static_cast<uint32>(EClassFlags::PerObjectConfig)) Result.push_back("PerObjectConfig");
    if (F & static_cast<uint32>(EClassFlags::ReplicationDataIsSetUp)) Result.push_back("ReplicationDataIsSetUp");
    if (F & static_cast<uint32>(EClassFlags::EditInlineNew)) Result.push_back("EditInlineNew");
    if (F & static_cast<uint32>(EClassFlags::CollapseCategories)) Result.push_back("CollapseCategories");
    if (F & static_cast<uint32>(EClassFlags::Interface)) Result.push_back("Interface");
    if (F & static_cast<uint32>(EClassFlags::CustomConstructor)) Result.push_back("CustomConstructor");
    if (F & static_cast<uint32>(EClassFlags::Const)) Result.push_back("Const");
    if (F & static_cast<uint32>(EClassFlags::LayoutChanging)) Result.push_back("LayoutChanging");
    if (F & static_cast<uint32>(EClassFlags::CompiledFromBlueprint)) Result.push_back("CompiledFromBlueprint");
    if (F & static_cast<uint32>(EClassFlags::MinimalAPI)) Result.push_back("MinimalAPI");
    if (F & static_cast<uint32>(EClassFlags::RequiredAPI)) Result.push_back("RequiredAPI");
    if (F & static_cast<uint32>(EClassFlags::DefaultToInstanced)) Result.push_back("DefaultToInstanced");
    if (F & static_cast<uint32>(EClassFlags::TokenStreamAssembled)) Result.push_back("TokenStreamAssembled");
    if (F & static_cast<uint32>(EClassFlags::HasInstancedReference)) Result.push_back("HasInstancedReference");
    if (F & static_cast<uint32>(EClassFlags::Hidden)) Result.push_back("Hidden");
    if (F & static_cast<uint32>(EClassFlags::Deprecated)) Result.push_back("Deprecated");
    if (F & static_cast<uint32>(EClassFlags::HideDropDown)) Result.push_back("HideDropDown");
    if (F & static_cast<uint32>(EClassFlags::GlobalUserConfig)) Result.push_back("GlobalUserConfig");
    if (F & static_cast<uint32>(EClassFlags::Intrinsic)) Result.push_back("Intrinsic");
    if (F & static_cast<uint32>(EClassFlags::Constructed)) Result.push_back("Constructed");
    if (F & static_cast<uint32>(EClassFlags::ConfigDoNotCheckDefaults)) Result.push_back("ConfigDoNotCheckDefaults");
    if (F & static_cast<uint32>(EClassFlags::NewerVersionExists)) Result.push_back("NewerVersionExists");
    
    return Result;
}

std::vector<std::string> GetPropertyFlagsArray(EPropertyFlags PropertyFlags)
{
    std::vector<std::string> RetFlags;

    if (PropertyFlags & EPropertyFlags::Edit) RetFlags.push_back("Edit");
    if (PropertyFlags & EPropertyFlags::ConstParm) RetFlags.push_back("ConstParm");
    if (PropertyFlags & EPropertyFlags::BlueprintVisible) RetFlags.push_back("BlueprintVisible");
    if (PropertyFlags & EPropertyFlags::ExportObject) RetFlags.push_back("ExportObject");
    if (PropertyFlags & EPropertyFlags::BlueprintReadOnly) RetFlags.push_back("BlueprintReadOnly");
    if (PropertyFlags & EPropertyFlags::Net) RetFlags.push_back("Net");
    if (PropertyFlags & EPropertyFlags::EditFixedSize) RetFlags.push_back("EditFixedSize");
    if (PropertyFlags & EPropertyFlags::Parm) RetFlags.push_back("Parm");
    if (PropertyFlags & EPropertyFlags::OutParm) RetFlags.push_back("OutParm");
    if (PropertyFlags & EPropertyFlags::ZeroConstructor) RetFlags.push_back("ZeroConstructor");
    if (PropertyFlags & EPropertyFlags::ReturnParm) RetFlags.push_back("ReturnParm");
    if (PropertyFlags & EPropertyFlags::DisableEditOnTemplate) RetFlags.push_back("DisableEditOnTemplate");
    if (PropertyFlags & EPropertyFlags::Transient) RetFlags.push_back("Transient");
    if (PropertyFlags & EPropertyFlags::Config) RetFlags.push_back("Config");
    if (PropertyFlags & EPropertyFlags::DisableEditOnInstance) RetFlags.push_back("DisableEditOnInstance");
    if (PropertyFlags & EPropertyFlags::EditConst) RetFlags.push_back("EditConst");
    if (PropertyFlags & EPropertyFlags::GlobalConfig) RetFlags.push_back("GlobalConfig");
    if (PropertyFlags & EPropertyFlags::InstancedReference) RetFlags.push_back("InstancedReference");
    if (PropertyFlags & EPropertyFlags::DuplicateTransient) RetFlags.push_back("DuplicateTransient");
    if (PropertyFlags & EPropertyFlags::SubobjectReference) RetFlags.push_back("SubobjectReference");
    if (PropertyFlags & EPropertyFlags::SaveGame) RetFlags.push_back("SaveGame");
    if (PropertyFlags & EPropertyFlags::NoClear) RetFlags.push_back("NoClear");
    if (PropertyFlags & EPropertyFlags::ReferenceParm) RetFlags.push_back("ReferenceParm");
    if (PropertyFlags & EPropertyFlags::BlueprintAssignable) RetFlags.push_back("BlueprintAssignable");
    if (PropertyFlags & EPropertyFlags::Deprecated) RetFlags.push_back("Deprecated");
    if (PropertyFlags & EPropertyFlags::IsPlainOldData) RetFlags.push_back("IsPlainOldData");
    if (PropertyFlags & EPropertyFlags::RepSkip) RetFlags.push_back("RepSkip");
    if (PropertyFlags & EPropertyFlags::RepNotify) RetFlags.push_back("RepNotify");
    if (PropertyFlags & EPropertyFlags::Interp) RetFlags.push_back("Interp");
    if (PropertyFlags & EPropertyFlags::NonTransactional) RetFlags.push_back("NonTransactional");
    if (PropertyFlags & EPropertyFlags::EditorOnly) RetFlags.push_back("EditorOnly");
    if (PropertyFlags & EPropertyFlags::NoDestructor) RetFlags.push_back("NoDestructor");
    if (PropertyFlags & EPropertyFlags::AutoWeak) RetFlags.push_back("AutoWeak");
    if (PropertyFlags & EPropertyFlags::ContainsInstancedReference) RetFlags.push_back("ContainsInstancedReference");
    if (PropertyFlags & EPropertyFlags::AssetRegistrySearchable) RetFlags.push_back("AssetRegistrySearchable");
    if (PropertyFlags & EPropertyFlags::SimpleDisplay) RetFlags.push_back("SimpleDisplay");
    if (PropertyFlags & EPropertyFlags::AdvancedDisplay) RetFlags.push_back("AdvancedDisplay");
    if (PropertyFlags & EPropertyFlags::Protected) RetFlags.push_back("Protected");
    if (PropertyFlags & EPropertyFlags::BlueprintCallable) RetFlags.push_back("BlueprintCallable");
    if (PropertyFlags & EPropertyFlags::BlueprintAuthorityOnly) RetFlags.push_back("BlueprintAuthorityOnly");
    if (PropertyFlags & EPropertyFlags::TextExportTransient) RetFlags.push_back("TextExportTransient");
    if (PropertyFlags & EPropertyFlags::NonPIEDuplicateTransient) RetFlags.push_back("NonPIEDuplicateTransient");
    if (PropertyFlags & EPropertyFlags::ExposeOnSpawn) RetFlags.push_back("ExposeOnSpawn");
    if (PropertyFlags & EPropertyFlags::PersistentInstance) RetFlags.push_back("PersistentInstance");
    if (PropertyFlags & EPropertyFlags::UObjectWrapper) RetFlags.push_back("UObjectWrapper");
    if (PropertyFlags & EPropertyFlags::HasGetValueTypeHash) RetFlags.push_back("HasGetValueTypeHash");
    if (PropertyFlags & EPropertyFlags::NativeAccessSpecifierPublic) RetFlags.push_back("NativeAccessSpecifierPublic");
    if (PropertyFlags & EPropertyFlags::NativeAccessSpecifierProtected) RetFlags.push_back("NativeAccessSpecifierProtected");
    if (PropertyFlags & EPropertyFlags::NativeAccessSpecifierPrivate) RetFlags.push_back("NativeAccessSpecifierPrivate");
    if (PropertyFlags & EPropertyFlags::TObjectPtr) RetFlags.push_back("TObjectPtr");

    return RetFlags;
}

std::vector<std::string> GetFunctionFlagsArray(EFunctionFlags FunctionFlags)
{
    std::vector<std::string> RetFlags;
    if (FunctionFlags & EFunctionFlags::Final) RetFlags.push_back("Final");
    if (FunctionFlags & EFunctionFlags::RequiredAPI) RetFlags.push_back("RequiredAPI");
    if (FunctionFlags & EFunctionFlags::BlueprintAuthorityOnly) RetFlags.push_back("BlueprintAuthorityOnly");
    if (FunctionFlags & EFunctionFlags::BlueprintCosmetic) RetFlags.push_back("BlueprintCosmetic");
    if (FunctionFlags & EFunctionFlags::Net) RetFlags.push_back("Net");
    if (FunctionFlags & EFunctionFlags::NetReliable) RetFlags.push_back("NetReliable");
    if (FunctionFlags & EFunctionFlags::NetRequest) RetFlags.push_back("NetRequest");
    if (FunctionFlags & EFunctionFlags::Exec) RetFlags.push_back("Exec");
    if (FunctionFlags & EFunctionFlags::Native) RetFlags.push_back("Native");
    if (FunctionFlags & EFunctionFlags::Event) RetFlags.push_back("Event");
    if (FunctionFlags & EFunctionFlags::NetResponse) RetFlags.push_back("NetResponse");
    if (FunctionFlags & EFunctionFlags::Static) RetFlags.push_back("Static");
    if (FunctionFlags & EFunctionFlags::NetMulticast) RetFlags.push_back("NetMulticast");
    if (FunctionFlags & EFunctionFlags::UbergraphFunction) RetFlags.push_back("UbergraphFunction");
    if (FunctionFlags & EFunctionFlags::MulticastDelegate) RetFlags.push_back("MulticastDelegate");
    if (FunctionFlags & EFunctionFlags::Public) RetFlags.push_back("Public");
    if (FunctionFlags & EFunctionFlags::Private) RetFlags.push_back("Private");
    if (FunctionFlags & EFunctionFlags::Protected) RetFlags.push_back("Protected");
    if (FunctionFlags & EFunctionFlags::Delegate) RetFlags.push_back("Delegate");
    if (FunctionFlags & EFunctionFlags::NetServer) RetFlags.push_back("NetServer");
    if (FunctionFlags & EFunctionFlags::HasOutParms) RetFlags.push_back("HasOutParms");
    if (FunctionFlags & EFunctionFlags::HasDefaults) RetFlags.push_back("HasDefaults");
    if (FunctionFlags & EFunctionFlags::NetClient) RetFlags.push_back("NetClient");
    if (FunctionFlags & EFunctionFlags::DLLImport) RetFlags.push_back("DLLImport");
    if (FunctionFlags & EFunctionFlags::BlueprintCallable) RetFlags.push_back("BlueprintCallable");
    if (FunctionFlags & EFunctionFlags::BlueprintEvent) RetFlags.push_back("BlueprintEvent");
    if (FunctionFlags & EFunctionFlags::BlueprintPure) RetFlags.push_back("BlueprintPure");
    if (FunctionFlags & EFunctionFlags::EditorOnly) RetFlags.push_back("EditorOnly");
    if (FunctionFlags & EFunctionFlags::Const) RetFlags.push_back("Const");
    if (FunctionFlags & EFunctionFlags::NetValidate) RetFlags.push_back("NetValidate");
    
    return RetFlags;
}

void InjectUHTPropertySpecifiers(nlohmann::ordered_json& Node, UEProperty Prop)
{
    EPropertyFlags Flags = Prop.GetPropertyFlags();

    if (Flags & EPropertyFlags::Edit)
    {
        if (Flags & EPropertyFlags::EditConst)
        {
            if (Flags & EPropertyFlags::DisableEditOnInstance) Node["VisibleDefaultsOnly"] = "true";
            else if (Flags & EPropertyFlags::DisableEditOnTemplate) Node["VisibleInstanceOnly"] = "true";
            else Node["VisibleAnywhere"] = "true";
        }
        else
        {
            if (Flags & EPropertyFlags::DisableEditOnInstance) Node["EditDefaultsOnly"] = "true";
            else if (Flags & EPropertyFlags::DisableEditOnTemplate) Node["EditInstanceOnly"] = "true";
            else Node["EditAnywhere"] = "true";
        }
    }
    
    if (Flags & EPropertyFlags::BlueprintVisible)
    {
        if (Flags & EPropertyFlags::BlueprintReadOnly) Node["BlueprintReadOnly"] = "true";
        else Node["BlueprintReadWrite"] = "true";
    }
    
    if (Flags & EPropertyFlags::BlueprintAssignable) Node["BlueprintAssignable"] = "true";
    if (Flags & EPropertyFlags::BlueprintCallable) Node["BlueprintCallable"] = "true";
    if (Flags & EPropertyFlags::BlueprintAuthorityOnly) Node["BlueprintAuthorityOnly"] = "true";

    if (Flags & EPropertyFlags::Transient) Node["Transient"] = "true";
    if (Flags & EPropertyFlags::DuplicateTransient) Node["DuplicateTransient"] = "true";
    if (Flags & EPropertyFlags::NonPIEDuplicateTransient) Node["NonPIEDuplicateTransient"] = "true";
    if (Flags & EPropertyFlags::TextExportTransient) Node["TextExportTransient"] = "true";
    if (Flags & EPropertyFlags::NoClear) Node["NoClear"] = "true";
    if (Flags & EPropertyFlags::EditFixedSize) Node["EditFixedSize"] = "true";
    if (Flags & EPropertyFlags::ExportObject) Node["Export"] = "true";

    if (Flags & EPropertyFlags::Net)
    {
        if (Flags & EPropertyFlags::RepNotify) 
        {
            FName RepFunc(static_cast<uint8*>(Prop.GetAddress()) + Off::Property::Offset_Internal + 4);
            std::string FuncName = RepFunc.ToString();
            
            if (!FuncName.empty() && FuncName != "None")
            {
                Node["ReplicatedUsing"] = FuncName;
            }
            else
            {
                Node["ReplicatedUsing"] = "true";
            }
        }
        else 
        {
            Node["Replicated"] = "true";
        }
    }
    if (Flags & EPropertyFlags::RepSkip) Node["NotReplicated"] = "true";
    
    if (Flags & EPropertyFlags::InstancedReference) Node["Instanced"] = "true";
    if (Flags & EPropertyFlags::SaveGame) Node["SaveGame"] = "true";
    if (Flags & EPropertyFlags::Config) Node["Config"] = "true";
    if (Flags & EPropertyFlags::GlobalConfig) Node["GlobalConfig"] = "true";
    if (Flags & EPropertyFlags::Interp) Node["Interp"] = "true";
    if (Flags & EPropertyFlags::NonTransactional) Node["NonTransactional"] = "true";
    
    if (Flags & EPropertyFlags::EditorOnly) Node["EditorOnly"] = "true";
    if (Flags & EPropertyFlags::AdvancedDisplay) Node["AdvancedDisplay"] = "true";
    if (Flags & EPropertyFlags::SimpleDisplay) Node["SimpleDisplay"] = "true";
    if (Flags & EPropertyFlags::AssetRegistrySearchable) Node["AssetRegistrySearchable"] = "true";
}

void InjectUHTFunctionSpecifiers(nlohmann::ordered_json& Node, EFunctionFlags Flags)
{
    if (Flags & EFunctionFlags::BlueprintCallable) Node["BlueprintCallable"] = "true";
    if (Flags & EFunctionFlags::BlueprintEvent) Node["BlueprintEvent"] = "true";
    if (Flags & EFunctionFlags::BlueprintPure) Node["BlueprintPure"] = "true";
    if (Flags & EFunctionFlags::BlueprintAuthorityOnly) Node["BlueprintAuthorityOnly"] = "true";
    if (Flags & EFunctionFlags::BlueprintCosmetic) Node["BlueprintCosmetic"] = "true";

    if (Flags & EFunctionFlags::NetServer) Node["Server"] = "true";
    if (Flags & EFunctionFlags::NetClient) Node["Client"] = "true";
    if (Flags & EFunctionFlags::NetMulticast) Node["NetMulticast"] = "true";
    if (Flags & EFunctionFlags::NetReliable) Node["Reliable"] = "true";

    if (Flags & EFunctionFlags::Exec) Node["Exec"] = "true";
    if (Flags & EFunctionFlags::Event) Node["Event"] = "true";
    if (Flags & EFunctionFlags::Static) Node["Static"] = "true";
}

std::string GetDefaultValueAsString(UEProperty Prop, void* Container)
{
    if (!Container) return "";
    
    uint8* ValuePtr = static_cast<uint8*>(Container) + Prop.GetOffset();

    if (Prop.IsA(EClassCastFlags::BoolProperty))
    {
        UEBoolProperty BoolProp = Prop.Cast<UEBoolProperty>();
        if (BoolProp.IsNativeBool())
        {
            return (*reinterpret_cast<bool*>(ValuePtr)) ? "true" : "false";
        }
        else
        {
            uint8 FieldMask = BoolProp.GetFieldMask();
            uint8 ByteValue = *reinterpret_cast<uint8*>(ValuePtr);
            return (ByteValue & FieldMask) ? "true" : "false";
        }
    }
    else if (Prop.IsA(EClassCastFlags::EnumProperty))
    {
        UEEnumProperty EnumProp = Prop.Cast<UEEnumProperty>();
        UEProperty UnderProp = EnumProp.GetUnderlayingProperty();
        
        int64 Value = 0;
        int32 Size = UnderProp.GetSize();
        
        if (Size == 1) Value = *reinterpret_cast<int8*>(ValuePtr);
        else if (Size == 2) Value = *reinterpret_cast<int16*>(ValuePtr);
        else if (Size == 4) Value = *reinterpret_cast<int32*>(ValuePtr);
        else if (Size == 8) Value = *reinterpret_cast<int64*>(ValuePtr);

        if (UEEnum Enum = EnumProp.GetEnum())
        {
            auto NameValuePairs = Enum.GetNameValuePairs();
            for (const auto& [ValueFName, Val] : NameValuePairs)
            {
                if (Val == Value)
                {
                    return ValueFName.ToString(); 
                }
            }
        }
        return std::to_string(Value);
    }
    else if (Prop.IsA(EClassCastFlags::ByteProperty)) 
    {
        UEByteProperty ByteProp = Prop.Cast<UEByteProperty>();
        uint8 ByteVal = *reinterpret_cast<uint8*>(ValuePtr);
        
        if (UEEnum Enum = ByteProp.GetEnum())
        {
            auto NameValuePairs = Enum.GetNameValuePairs();
            for (const auto& [ValueFName, Val] : NameValuePairs)
            {
                if (Val == ByteVal)
                {
                    return ValueFName.ToString();
                }
            }
        }
        return std::to_string(ByteVal);
    }
    else if (Prop.IsA(EClassCastFlags::IntProperty)) return std::to_string(*reinterpret_cast<int32*>(ValuePtr));
    else if (Prop.IsA(EClassCastFlags::FloatProperty)) return std::to_string(*reinterpret_cast<float*>(ValuePtr));
    else if (Prop.IsA(EClassCastFlags::DoubleProperty)) return std::to_string(*reinterpret_cast<double*>(ValuePtr));
    else if (Prop.IsA(EClassCastFlags::Int16Property)) return std::to_string(*reinterpret_cast<int16*>(ValuePtr));
    else if (Prop.IsA(EClassCastFlags::UInt16Property)) return std::to_string(*reinterpret_cast<uint16*>(ValuePtr));
    else if (Prop.IsA(EClassCastFlags::UInt32Property)) return std::to_string(*reinterpret_cast<uint32*>(ValuePtr));
    else if (Prop.IsA(EClassCastFlags::NameProperty)) 
    {
        std::string NameStr = FName(ValuePtr).ToString();
        return NameStr == "None" ? "" : NameStr;
    }
    else if (Prop.IsA(EClassCastFlags::StrProperty)) 
    {
        UC::FString* StrPtr = reinterpret_cast<UC::FString*>(ValuePtr);
        return StrPtr->IsValid() ? StrPtr->ToString() : "";
    }
    
    return ""; 
}

std::string GetPrecisePropertyType(UEProperty Prop, int32 NextOffset, const std::unordered_map<std::string, std::string>& MetaData)
{
    // Execute Padding Logic First
    if (Prop.IsA(EClassCastFlags::BoolProperty))
    {
        UEBoolProperty BoolProp = Prop.Cast<UEBoolProperty>();
        if (!BoolProp.IsNativeBool())
        {
            int32 Diff = NextOffset - Prop.GetOffset();
            
            if (Diff >= 4) return "uint32 : 1";
            if (Diff >= 2) return "uint16 : 1";
            return "uint8 : 1";
        }
    }

    auto CppTypeIt = MetaData.find("CPP_Type");
    if (CppTypeIt != MetaData.end()) 
    {
        std::string ExactType = CppTypeIt->second;
        if (Prop.IsA(EClassCastFlags::BoolProperty) && ExactType.find(':') == std::string::npos) {
            return ExactType + " : 1";
        }
        return ExactType;
    }

    return Prop.GetCppType();
}

void DumpEditorOnlyMetadata(const fs::path& DumperFolder)
{
    nlohmann::ordered_json MetadataJson;
    MetadataJson["GameName"] = Settings::Generator::GameName;
    MetadataJson["GameVersion"] = Settings::Generator::GameVersion;

    struct alignas(0x4) Name08Byte { uint8 Pad[0x08]; };
    struct alignas(0x4) Name12Byte { uint8 Pad[0x0C]; };
    struct alignas(0x4) Name16Byte { uint8 Pad[0x10]; };

    int32 CachedClassFlagsOffset = 0x0;
    int32 CachedClassConfigNameOffset = 0x0;

    UEClass CoreObjClass = ObjectArray::FindClassFast("Object");
    if (CoreObjClass) 
    {
        uint8* Base = static_cast<uint8*>(CoreObjClass.GetAddress());
        
        for (int32 i = Off::UClass::CastFlags - 0x20; i <= Off::UClass::CastFlags; i += 4) {
            if (i < 0) continue;
            uint32 Flags = *reinterpret_cast<uint32*>(Base + i);
            if ((Flags & 0x10000081) == 0x10000081) { 
                CachedClassFlagsOffset = i;
                break;
            }
        }

        for (int32 i = Off::UClass::CastFlags; i < Off::UClass::ClassDefaultObject; i += 4) {
            FName potentialName = FName(Base + i);
            if (potentialName.GetCompIdx() > 0 && potentialName.GetCompIdx() < 2000000) { 
                if (potentialName.ToString() == "Engine") {
                    CachedClassConfigNameOffset = i;
                    break;
                }
            }
        }
    }

    std::unordered_map<void*, std::unordered_map<std::string, std::string>> GlobalMetadataCache;
    std::unordered_map<void*, std::string> EnumUnderlyingTypes;
    UEClass MetaDataClass = nullptr;
    
    for (UEObject Obj : ObjectArray())
    {
        if (!Obj) continue;
        if (Obj.IsA(EClassCastFlags::Struct))
        {
            UEStruct Struct = Obj.Cast<UEStruct>();
            for (UEProperty Prop : Struct.GetProperties())
            {
                if (Prop.IsA(EClassCastFlags::EnumProperty)) {
                    UEEnumProperty EnumProp = Prop.Cast<UEEnumProperty>();
                    if (UEEnum Enum = EnumProp.GetEnum()) {
                        if (UEProperty UnderProp = EnumProp.GetUnderlayingProperty())
                            EnumUnderlyingTypes[Enum.GetAddress()] = UnderProp.GetCppType();
                    }
                }
                else if (Prop.IsA(EClassCastFlags::ByteProperty)) {
                    UEByteProperty ByteProp = Prop.Cast<UEByteProperty>();
                    if (UEEnum Enum = ByteProp.GetEnum())
                        EnumUnderlyingTypes[Enum.GetAddress()] = "uint8";
                }
            }
        }
        if (Obj.IsA(EClassCastFlags::Class) && Obj.GetName() == "MetaData")
            MetaDataClass = Obj.Cast<UEClass>();
    }

    if (MetaDataClass)
    {
        const int32 ObjectMetaDataMapOffset = Off::UObject::Outer + sizeof(void*); 
        for (UEObject Obj : ObjectArray())
        {
           if (!Obj || !Obj.IsA(MetaDataClass)) continue;
           auto ExtractMetaDataMap = [&]<typename NameType>()
           {
               using UObjMetaDataMap = UC::TMap<FWeakObjectPtr, UC::TMap<NameType, UC::FString>>;
               auto* ObjMap = reinterpret_cast<UObjMetaDataMap*>(static_cast<uint8*>(Obj.GetAddress()) + ObjectMetaDataMapOffset);
               if (!IsSafeMap(ObjMap)) return;
               for (const auto& [WeakPtrKey, ValueMap] : *ObjMap)
               {
                  if (WeakPtrKey.ObjectIndex < 0 || WeakPtrKey.ObjectIndex >= ObjectArray::Num()) continue;
                  UEObject TargetObj = ObjectArray::GetByIndex(WeakPtrKey.ObjectIndex);
                  if (!TargetObj) continue;
                  void* RealAddress = TargetObj.GetAddress();
                  if (!RealAddress || !IsSafeMap(&ValueMap)) continue;
                  auto& CachedMap = GlobalMetadataCache[RealAddress];
                  for (const auto& [Key, Value] : ValueMap) {
                     std::string KeyStr = FName((void*)&Key).ToString();
                     std::string ValStr = Value.ToString();
                     if (!KeyStr.empty()) CachedMap[KeyStr] = ValStr.empty() ? "true" : ValStr;
                  }
               }
           };
           if (Off::InSDK::Name::FNameSize >= 0x10) ExtractMetaDataMap.template operator()<Name16Byte>();
           else if (Off::InSDK::Name::FNameSize >= 0xC) ExtractMetaDataMap.template operator()<Name12Byte>();
           else ExtractMetaDataMap.template operator()<Name08Byte>();
        }
    }

    const bool bDefaultToOldStructMacro = true; 

    for (UEObject Obj : ObjectArray())
    {
        if (!Obj) continue;
        
        if (Obj.IsA(EClassCastFlags::Function) && !Obj.IsA(EClassCastFlags::DelegateFunction)) 
            continue;

        std::string PackageName = "";
        UEObject Outermost = Obj.GetOutermost();
        if (Outermost) {
            PackageName = Outermost.GetName();
            if (PackageName.starts_with("/Script/")) PackageName = PackageName.substr(8);
        }

        if (Obj.IsA(EClassCastFlags::DelegateFunction))
        {
            UEFunction Delegate = Obj.Cast<UEFunction>();
            std::string CppName = Delegate.GetName();
            auto& DelegateNode = MetadataJson[CppName];
            
            DelegateNode["MemberType"] = "Delegate";
            EFunctionFlags FFlags = Delegate.GetFunctionFlags();
            DelegateNode["FunctionFlags"] = GetFunctionFlagsArray(FFlags);
            InjectUHTFunctionSpecifiers(DelegateNode, FFlags);

            UEObject Outer = Delegate.GetOuter();
            if (Outer) DelegateNode["DeclaredIn"] = Outer.GetName();
            if (!PackageName.empty()) DelegateNode["Package"] = PackageName;

            std::string DelegateIncPath = "";
            std::string DelegateModRelPath = "";

            if (GlobalMetadataCache.contains(Delegate.GetAddress())) {
                for (const auto& [MetaKey, MetaVal] : GlobalMetadataCache[Delegate.GetAddress()]) {
                    DelegateNode[MetaKey] = MetaVal; 
                    if (MetaKey == "IncludePath") DelegateIncPath = MetaVal;
                    if (MetaKey == "ModuleRelativePath") DelegateModRelPath = MetaVal;
                }
            }

            std::string AbsPath = ResolveHeaderPath(DelegateIncPath, DelegateModRelPath, PackageName, DumperFolder);
            if (!AbsPath.empty()) DelegateNode["AbsoluteHeaderPath"] = AbsPath;

            auto FuncProperties = Delegate.GetProperties();
            if (!FuncProperties.empty())
            {
                std::sort(FuncProperties.begin(), FuncProperties.end(), [](const UEProperty& A, const UEProperty& B) { return A.GetOffset() < B.GetOffset(); });
                auto& ParamsNode = DelegateNode["Parameters"];
                
                int32 StructEndOffset = Delegate.GetStructSize();
                for (UEProperty Param : FuncProperties)
                {
                    nlohmann::ordered_json ParamNode = nlohmann::ordered_json::object();
                    
                    int32 CurrentNextOffset = StructEndOffset;
                    for (UEProperty P : FuncProperties) {
                        if (P.GetOffset() > Param.GetOffset() && P.GetOffset() < CurrentNextOffset) {
                            CurrentNextOffset = P.GetOffset();
                        }
                    }
                    
                    std::unordered_map<std::string, std::string> PropMeta;
                    if (GlobalMetadataCache.contains(Param.GetAddress())) {
                        PropMeta = GlobalMetadataCache[Param.GetAddress()];
                    }

                    ParamNode["PropertyType"] = GetPrecisePropertyType(Param, CurrentNextOffset, PropMeta);
                    
                    int32 ArrayDim = Param.GetArrayDim();
                    if (ArrayDim > 1) {
                        ParamNode["ArrayDim"] = std::to_string(ArrayDim);
                    }
                    
                    EPropertyFlags ParamFlags = Param.GetPropertyFlags();
                    ParamNode["PropertyFlags"] = GetPropertyFlagsArray(ParamFlags);
                    InjectUHTPropertySpecifiers(ParamNode, Param);
                    
                    if (Settings::Internal::bUseFProperty) 
                    {
                        UEFField Field = Param.Cast<UEFField>();
                        if (Field) 
                        {
                            for (const auto& [MetaKey, MetaVal] : Field.GetMetaData()) 
                            {
                                if (!MetaKey.empty()) ParamNode[MetaKey] = MetaVal.empty() ? "true" : MetaVal;
                            }
                        }
                    }
                    
                    if (!PropMeta.empty()) 
                    {
                        for (const auto& [MetaKey, MetaVal] : PropMeta) 
                        {
                            ParamNode[MetaKey] = MetaVal;
                        }
                    }
                    ParamsNode[Param.GetValidName()] = ParamNode;
                }
            }
        }
        else if (Obj.IsA(EClassCastFlags::Struct))
        {
            UEStruct Struct = Obj.Cast<UEStruct>();
            std::string CppName = Struct.GetCppName();
            auto& StructNode = MetadataJson[CppName];
            
            UEStruct Super = Struct.GetSuper();
            if (Super) StructNode["SuperStruct"] = Super.GetCppName();
            if (!PackageName.empty()) StructNode["Package"] = PackageName;

            bool bHasObjectInitializer = false;
            std::string StructIncPath = "";
            std::string StructModRelPath = "";

            if (GlobalMetadataCache.contains(Struct.GetAddress()))
            {
                for (const auto& [MetaKey, MetaVal] : GlobalMetadataCache[Struct.GetAddress()])
                {
                    StructNode[MetaKey] = MetaVal; 
                    if (MetaKey == "IncludePath") StructIncPath = MetaVal;
                    if (MetaKey == "ModuleRelativePath") StructModRelPath = MetaVal;
                    
                    if (MetaKey == "ObjectInitializerConstructorDeclared") bHasObjectInitializer = true;
                    if (MetaKey == "IsBlueprintBase") StructNode["Blueprintable"] = MetaVal;
                }
            }

            std::string AbsPath = ResolveHeaderPath(StructIncPath, StructModRelPath, PackageName, DumperFolder);
            if (!AbsPath.empty()) StructNode["AbsoluteHeaderPath"] = AbsPath;

            void* CDO = nullptr;
            if (Obj.IsA(EClassCastFlags::Class))
            {
                StructNode["UHTMacro"] = bHasObjectInitializer ? "GENERATED_UCLASS_BODY()" : "GENERATED_BODY()";
                UEClass Clss = Obj.Cast<UEClass>();
                UEObject CDO_Obj = Clss.GetDefaultObject();
                if (CDO_Obj) CDO = CDO_Obj.GetAddress();

                if (CachedClassFlagsOffset != 0x0)
                {
                    EClassFlags ClassFlags = *reinterpret_cast<EClassFlags*>(static_cast<uint8*>(Struct.GetAddress()) + CachedClassFlagsOffset);
                    StructNode["ClassFlags"] = GetClassFlagsArray(ClassFlags);
                    
                    bool bIsExported = false;
                    if (static_cast<uint32>(ClassFlags) & static_cast<uint32>(EClassFlags::RequiredAPI)) {
                        StructNode["ClassAPI"] = "RequiredAPI";
                        bIsExported = true;
                    }
                    else if (static_cast<uint32>(ClassFlags) & static_cast<uint32>(EClassFlags::MinimalAPI)) {
                        StructNode["ClassAPI"] = "MinimalAPI";
                        bIsExported = true;
                    }
                    else {
                        StructNode["ClassAPI"] = "None";
                    }

                    if (bIsExported)
                    {
                        std::string ModuleName = PackageName;
                        std::transform(ModuleName.begin(), ModuleName.end(), ModuleName.begin(), ::toupper);
                        StructNode["ModuleAPI"] = ModuleName + "_API";
                    }

                    if (static_cast<uint32>(ClassFlags) & static_cast<uint32>(EClassFlags::Config)) {
                        if (CachedClassConfigNameOffset != 0x0) {
                            FName ConfigFName = FName(static_cast<uint8*>(Struct.GetAddress()) + CachedClassConfigNameOffset);
                            std::string ConfigStr = ConfigFName.ToString();
                            StructNode["Config"] = ConfigStr.empty() ? "true" : ConfigStr;
                        } else StructNode["Config"] = "true";
                    }
                }
                TArray<FImplementedInterface> Interfaces = Clss.GetImplementedInterfaces();
                if (Interfaces.Num() > 0) {
                    nlohmann::ordered_json InterfacesArray = nlohmann::ordered_json::array();
                    for (int i = 0; i < Interfaces.Num(); ++i) if (Interfaces[i].InterfaceClass) InterfacesArray.push_back(Interfaces[i].InterfaceClass.GetName());
                    if (!InterfacesArray.empty()) StructNode["Interfaces"] = InterfacesArray;
                }
            }
            else 
            {
                StructNode["UHTMacro"] = bDefaultToOldStructMacro ? "GENERATED_USTRUCT_BODY()" : "GENERATED_BODY()";
            }

            nlohmann::ordered_json MembersNode = nlohmann::ordered_json::object();
            
            auto Properties = Struct.GetProperties();
            if (!Properties.empty())
            {
                std::sort(Properties.begin(), Properties.end(), [](const UEProperty& A, const UEProperty& B) { return A.GetOffset() < B.GetOffset(); });
                int32 StructEndOffset = Struct.GetStructSize();
                
                for (UEProperty Prop : Properties)
                {
                    nlohmann::ordered_json VarNode = nlohmann::ordered_json::object();
                    VarNode["MemberType"] = "Variable";
                    std::string Accessibility = "Public"; 
                    if (Prop.HasPropertyFlags(EPropertyFlags::NativeAccessSpecifierProtected)) Accessibility = "Protected";
                    else if (Prop.HasPropertyFlags(EPropertyFlags::NativeAccessSpecifierPrivate)) Accessibility = "Private";
                    VarNode["Accessibility"] = Accessibility;

                    int32 CurrentNextOffset = StructEndOffset;
                    for (UEProperty P : Properties) {
                        if (P.GetOffset() > Prop.GetOffset() && P.GetOffset() < CurrentNextOffset) {
                            CurrentNextOffset = P.GetOffset();
                        }
                    }

                    std::unordered_map<std::string, std::string> PropMeta;
                    if (GlobalMetadataCache.contains(Prop.GetAddress())) {
                        PropMeta = GlobalMetadataCache[Prop.GetAddress()];
                    }

                    VarNode["PropertyType"] = GetPrecisePropertyType(Prop, CurrentNextOffset, PropMeta);

                    int32 ArrayDim = Prop.GetArrayDim();
                    if (ArrayDim > 1) {
                        VarNode["ArrayDim"] = std::to_string(ArrayDim);
                    }

                    if (Prop.IsA(EClassCastFlags::DelegateProperty) || Prop.IsA(EClassCastFlags::MulticastDelegateProperty) || Prop.IsA(EClassCastFlags::MulticastInlineDelegateProperty))
                    {
                        void* SignatureFuncPtr = *reinterpret_cast<void**>(static_cast<uint8*>(Prop.GetAddress()) + Off::DelegateProperty::SignatureFunction);
                        if (SignatureFuncPtr) VarNode["DelegateSignature"] = UEObject(SignatureFuncPtr).GetName();
                    }
                    
                    EPropertyFlags PFlags = Prop.GetPropertyFlags();
                    VarNode["PropertyFlags"] = GetPropertyFlagsArray(PFlags);
                    InjectUHTPropertySpecifiers(VarNode, Prop);

                    if (CDO) {
                        std::string DefVal = GetDefaultValueAsString(Prop, CDO);
                        if (!DefVal.empty()) VarNode["DefaultValue"] = DefVal;
                    }
                    
                    if (Settings::Internal::bUseFProperty) 
                    {
                        UEFField Field = Prop.Cast<UEFField>();
                        if (Field) 
                        {
                            for (const auto& [MetaKey, MetaVal] : Field.GetMetaData()) 
                            {
                                if (!MetaKey.empty()) VarNode[MetaKey] = MetaVal.empty() ? "true" : MetaVal;
                            }
                        }
                    }
                    
                    if (!PropMeta.empty()) 
                    {
                        for (const auto& [MetaKey, MetaVal] : PropMeta) 
                        {
                            VarNode[MetaKey] = MetaVal;
                        }
                    }
                    
                    MembersNode[Prop.GetValidName()] = VarNode;
                }
            }
            
            auto Functions = Struct.GetFunctions();
            if (!Functions.empty())
            {
                std::reverse(Functions.begin(), Functions.end());
                
                for (UEFunction Func : Functions)
                {
                    nlohmann::ordered_json FuncNode = nlohmann::ordered_json::object();
                    FuncNode["MemberType"] = "Function";
                    EFunctionFlags FFlags = Func.GetFunctionFlags();
                    FuncNode["FunctionFlags"] = GetFunctionFlagsArray(FFlags);
                    InjectUHTFunctionSpecifiers(FuncNode, FFlags);
                    
                    if (GlobalMetadataCache.contains(Func.GetAddress())) {
                        for (const auto& [MetaKey, MetaVal] : GlobalMetadataCache[Func.GetAddress()]) FuncNode[MetaKey] = MetaVal;
                    }
                    
                    auto FuncProperties = Func.GetProperties();
                    if (!FuncProperties.empty())
                    {
                        std::sort(FuncProperties.begin(), FuncProperties.end(), [](const UEProperty& A, const UEProperty& B) { return A.GetOffset() < B.GetOffset(); });
                        auto& ParamsNode = FuncNode["Parameters"];
                        int32 StructEndOffset = Func.GetStructSize();
                        
                        for (UEProperty Param : FuncProperties)
                        {
                            nlohmann::ordered_json ParamNode = nlohmann::ordered_json::object();
                            
                            int32 CurrentNextOffset = StructEndOffset;
                            for (UEProperty P : FuncProperties) {
                                if (P.GetOffset() > Param.GetOffset() && P.GetOffset() < CurrentNextOffset) {
                                    CurrentNextOffset = P.GetOffset();
                                }
                            }
                            
                            std::unordered_map<std::string, std::string> PropMeta;
                            if (GlobalMetadataCache.contains(Param.GetAddress())) {
                                PropMeta = GlobalMetadataCache[Param.GetAddress()];
                            }

                            ParamNode["PropertyType"] = GetPrecisePropertyType(Param, CurrentNextOffset, PropMeta);
                            
                            int32 ArrayDim = Param.GetArrayDim();
                            if (ArrayDim > 1) {
                                ParamNode["ArrayDim"] = std::to_string(ArrayDim);
                            }
                            
                            EPropertyFlags ParamFlags = Param.GetPropertyFlags();
                            ParamNode["PropertyFlags"] = GetPropertyFlagsArray(ParamFlags);
                            InjectUHTPropertySpecifiers(ParamNode, Param);
                            
                            if (Settings::Internal::bUseFProperty) 
                            {
                                UEFField Field = Param.Cast<UEFField>();
                                if (Field) 
                                {
                                    for (const auto& [MetaKey, MetaVal] : Field.GetMetaData()) 
                                    {
                                        if (!MetaKey.empty()) ParamNode[MetaKey] = MetaVal.empty() ? "true" : MetaVal;
                                    }
                                }
                            }
                            
                            if (!PropMeta.empty()) 
                            {
                                for (const auto& [MetaKey, MetaVal] : PropMeta) 
                                {
                                    ParamNode[MetaKey] = MetaVal;
                                }
                            }
                            ParamsNode[Param.GetValidName()] = ParamNode;
                        }
                    }
                    MembersNode[Func.GetValidName()] = FuncNode;
                }
            }
            if (!MembersNode.empty()) StructNode["Members"] = MembersNode;
            if (StructNode.empty()) MetadataJson.erase(CppName);
        }
        else if (Obj.IsA(EClassCastFlags::Enum))
        {
            UEEnum Enum = Obj.Cast<UEEnum>();
            std::string CppName = Enum.GetEnumPrefixedName();
            auto& EnumNode = MetadataJson[CppName];
            
            uint8 CppForm = *reinterpret_cast<uint8*>(static_cast<uint8*>(Enum.GetAddress()) + Off::UEnum::Names + 0x10);
            if (CppForm == 0) EnumNode["CppForm"] = "Regular";
            else if (CppForm == 1) { EnumNode["CppForm"] = "Namespaced"; EnumNode["CppType"] = CppName + "::Type"; }
            else if (CppForm == 2) EnumNode["CppForm"] = "EnumClass";

            std::string UnderlyingType = "uint8"; 
            if (EnumUnderlyingTypes.contains(Enum.GetAddress())) UnderlyingType = EnumUnderlyingTypes[Enum.GetAddress()];
            else {
                for (const auto& [ValueFName, Value] : Enum.GetNameValuePairs()) if (Value < 0 || Value > 255) { UnderlyingType = "int32"; break; }
            }
            EnumNode["UnderlyingType"] = UnderlyingType;
            if (!PackageName.empty()) EnumNode["Package"] = PackageName;
            
            std::string EnumIncPath = "";
            std::string EnumModRelPath = "";

            if (GlobalMetadataCache.contains(Enum.GetAddress())) {
                auto& EnumMeta = GlobalMetadataCache[Enum.GetAddress()];
                for (const auto& [MetaKey, MetaVal] : EnumMeta) {
                    if (MetaKey.find('.') == std::string::npos) { 
                        EnumNode[MetaKey] = MetaVal;
                        if (MetaKey == "IncludePath") EnumIncPath = MetaVal;
                        if (MetaKey == "ModuleRelativePath") EnumModRelPath = MetaVal;
                    }
                }
            }

            std::string AbsPath = ResolveHeaderPath(EnumIncPath, EnumModRelPath, PackageName, DumperFolder);
            if (!AbsPath.empty()) EnumNode["AbsoluteHeaderPath"] = AbsPath;

            auto NameValuePairs = Enum.GetNameValuePairs();
            if (!NameValuePairs.empty())
            {
                nlohmann::ordered_json ValuesNode = nlohmann::ordered_json::object();
                for (const auto& [ValueFName, Value] : NameValuePairs)
                {
                    std::string FullName = ValueFName.ToString();
                    if (FullName.ends_with("_MAX")) continue;
                    std::string ShortName = FullName;
                    size_t ColonPos = FullName.rfind("::");
                    if (ColonPos != std::string::npos) ShortName = FullName.substr(ColonPos + 2);
                    
                    nlohmann::ordered_json ValueNode = nlohmann::ordered_json::object();
                    ValueNode["Index"] = Value;
                    ValueNode["Name"] = FullName;
                    
                    std::string MetaPrefix = ShortName + ".";
                    if (GlobalMetadataCache.contains(Enum.GetAddress())) {
                        auto& EnumMeta = GlobalMetadataCache[Enum.GetAddress()];
                        for (const auto& [MetaKey, MetaVal] : EnumMeta) {
                            if (MetaKey.starts_with(MetaPrefix)) ValueNode[MetaKey.substr(MetaPrefix.length())] = MetaVal.empty() ? "true" : MetaVal;
                        }
                    }
                    ValuesNode[ShortName] = ValueNode;
                }
                if (!ValuesNode.empty()) EnumNode["Values"] = ValuesNode;
            }
            if (EnumNode.empty()) MetadataJson.erase(CppName);
        }
    }
    
    if (MetadataJson.size() > 2) {
        std::ofstream MetadataFile(DumperFolder / "Metadata.json");
        MetadataFile << MetadataJson.dump(4);
    }
}