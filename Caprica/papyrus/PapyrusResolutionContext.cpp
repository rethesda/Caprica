#include <papyrus/PapyrusResolutionContext.h>

#include <boost/filesystem.hpp>
#include <boost/range/adaptor/reversed.hpp>

#include <CapricaConfig.h>

#include <papyrus/PapyrusObject.h>
#include <papyrus/PapyrusScript.h>
#include <papyrus/PapyrusStruct.h>

#include <papyrus/parser/PapyrusParser.h>

namespace caprica { namespace papyrus {

PapyrusResolutionContext::~PapyrusResolutionContext() {
  for (auto& s : loadedScripts) {
    delete s.second;
  }
}

void PapyrusResolutionContext::addImport(std::string import) {
  auto sc = loadScript(import);
  if (!sc)
    fatalError("Failed to find imported script '" + import + ".psc'!");
  importedScripts.push_back(sc);
}

PapyrusScript* PapyrusResolutionContext::loadScript(std::string name) {
  auto f = loadedScripts.find(name);
  if (f != loadedScripts.end())
    return f->second;

  for (auto& dir : CapricaConfig::importDirectories) {
    if (boost::filesystem::exists(dir + name + ".psc")) {
      auto parser = new parser::PapyrusParser(dir + name + ".psc");
      auto a = parser->parseScript();
      delete parser;
      loadedScripts.insert({ a->objects[0]->name, a });
      auto ctx = new PapyrusResolutionContext();
      ctx->loadedScripts = loadedScripts;
      ctx->isExternalResolution = true;
      a->semantic(ctx);
      loadedScripts = ctx->loadedScripts;
      ctx->loadedScripts.clear();
      delete ctx;
      return a;
    }
  }

  return nullptr;
}

PapyrusType PapyrusResolutionContext::resolveType(PapyrusType tp) {
  if (tp.type != PapyrusType::Kind::Unresolved) {
    if (tp.type == PapyrusType::Kind::Array) {
      *tp.arrayElementType = resolveType(*tp.arrayElementType);
    }
    return tp;
  }

  if (CapricaConfig::enableDecompiledStructNameRefs) {
    auto pos = tp.name.find_first_of('#');
    if (pos != std::string::npos) {
      auto scName = tp.name.substr(0, pos);
      auto strucName = tp.name.substr(pos + 1);
      auto sc = loadScript(scName);
      if (!sc)
        fatalError("Unable to find script '" + scName + "' referenced by '" + tp.name + "'!");

      for (auto obj : sc->objects) {
        for (auto struc : obj->structs) {
          if (!_stricmp(struc->name.c_str(), strucName.c_str())) {
            tp.type = PapyrusType::Kind::ResolvedStruct;
            tp.resolvedStruct = struc;
            return tp;
          }
        }
      }

      fatalError("Unable to resolve a struct named '" + strucName + "' in script '" + scName + "'!");
    }
  }

  if (object) {
    for (auto& s : object->structs) {
      if (!_stricmp(s->name.c_str(), tp.name.c_str())) {
        tp.type = PapyrusType::Kind::ResolvedStruct;
        tp.resolvedStruct = s;
        return tp;
      }
    }

    if (!_stricmp(object->name.c_str(), tp.name.c_str())) {
      tp.type = PapyrusType::Kind::ResolvedObject;
      tp.resolvedObject = object;
      return tp;
    }
  }

  for (auto sc : importedScripts) {
    for (auto obj : sc->objects) {
      for (auto struc : obj->structs) {
        if (!_stricmp(struc->name.c_str(), tp.name.c_str())) {
          tp.type = PapyrusType::Kind::ResolvedStruct;
          tp.resolvedStruct = struc;
          return tp;
        }
      }
    }
  }

  auto sc = loadScript(tp.name);
  if (sc != nullptr) {
    for (auto obj : sc->objects) {
      if (!_stricmp(obj->name.c_str(), tp.name.c_str())) {
        tp.type = PapyrusType::Kind::ResolvedObject;
        tp.resolvedObject = obj;
        return tp;
      }
    }
  }
  
  fatalError("Unable to resolve type '" + tp.name + "'!");
}

PapyrusIdentifier PapyrusResolutionContext::tryResolveIdentifier(const PapyrusIdentifier& ident) const {
  if (ident.type != PapyrusIdentifierType::Unresolved) {
    return ident;
  }

  for (auto& stack : boost::adaptors::reverse(identifierStack)) {
    auto f = stack.find(ident.name);
    if (f != stack.end()) {
      return f->second;
    }
  }

  if (object->parentClass != PapyrusType::None()) {
    if (object->parentClass.type != PapyrusType::Kind::ResolvedObject)
      fatalError("Something is wrong here, this should already have been resolved!");
    return tryResolveMemberIdentifier(object->parentClass, ident);
  }

  return ident;
}

PapyrusIdentifier PapyrusResolutionContext::resolveMemberIdentifier(const PapyrusType& baseType, const PapyrusIdentifier& ident) const {
  auto id = tryResolveMemberIdentifier(baseType, ident);
  if (id.type == PapyrusIdentifierType::Unresolved)
    throw std::runtime_error("Unresolved identifier '" + ident.name + "'!");
  return id;
}

PapyrusIdentifier PapyrusResolutionContext::tryResolveMemberIdentifier(const PapyrusType& baseType, const PapyrusIdentifier& ident) const {
  if (ident.type != PapyrusIdentifierType::Unresolved) {
    return ident;
  }

  if (baseType.type == PapyrusType::Kind::ResolvedStruct) {
    for (auto& sm : baseType.resolvedStruct->members) {
      if (!_stricmp(sm->name.c_str(), ident.name.c_str())) {
        PapyrusIdentifier id = ident;
        id.type = PapyrusIdentifierType::StructMember;
        id.structMember = sm;
        return id;
      }
    }
  } else if (baseType.type == PapyrusType::Kind::ResolvedObject) {
    for (auto& propGroup : baseType.resolvedObject->propertyGroups) {
      for (auto& prop : propGroup->properties) {
        if (!_stricmp(prop->name.c_str(), ident.name.c_str())) {
          PapyrusIdentifier id = ident;
          id.type = PapyrusIdentifierType::Property;
          id.prop = prop;
          return id;
        }
      }
    }

    if (baseType.resolvedObject->parentClass != PapyrusType::None()) {
      if (baseType.resolvedObject->parentClass.type != PapyrusType::Kind::ResolvedObject)
        fatalError("Something is wrong here, this should already have been resolved!");

      return tryResolveMemberIdentifier(baseType.resolvedObject->parentClass, ident);
    }
  }

  return ident;
}

PapyrusIdentifier PapyrusResolutionContext::resolveFunctionIdentifier(const PapyrusType& baseType, const PapyrusIdentifier& ident) const {
  if (ident.type != PapyrusIdentifierType::Unresolved) {
    return ident;
  }

  if (baseType == PapyrusType::None()) {
    for (auto& state : object->states) {
      for (auto& func : state->functions) {
        if (!_stricmp(func->name.c_str(), ident.name.c_str())) {
          PapyrusIdentifier id = ident;
          id.type = PapyrusIdentifierType::Function;
          id.func = func;
          return id;
        }
      }
    }

    for (auto sc : importedScripts) {
      for (auto obj : sc->objects) {
        for (auto stat : obj->states) {
          for (auto func : stat->functions) {
            if (func->isGlobal && !_stricmp(func->name.c_str(), ident.name.c_str())) {
              PapyrusIdentifier id = ident;
              id.type = PapyrusIdentifierType::Function;
              id.func = func;
              return id;
            }
          }
        }
      }
    }
  } else if (baseType.type == PapyrusType::Kind::Array) {
    PapyrusIdentifier id = ident;
    id.type = PapyrusIdentifierType::BuiltinArrayFunction;
    id.arrayFuncElementType = baseType.getElementType();
    if (!_stricmp(ident.name.c_str(), "find")) {
      if (baseType.arrayElementType->type == PapyrusType::Kind::ResolvedStruct)
        id.arrayFuncKind = PapyrusBuiltinArrayFunctionKind::FindStruct;
      else
        id.arrayFuncKind = PapyrusBuiltinArrayFunctionKind::Find;
    } else if (!_stricmp(ident.name.c_str(), "rfind")) {
      if (baseType.arrayElementType->type == PapyrusType::Kind::ResolvedStruct)
        id.arrayFuncKind = PapyrusBuiltinArrayFunctionKind::RFindStruct;
      else
        id.arrayFuncKind = PapyrusBuiltinArrayFunctionKind::RFind;
    } else if (!_stricmp(ident.name.c_str(), "add")) {
      id.arrayFuncKind = PapyrusBuiltinArrayFunctionKind::Add;
    } else if (!_stricmp(ident.name.c_str(), "clear")) {
      id.arrayFuncKind = PapyrusBuiltinArrayFunctionKind::Clear;
    } else if (!_stricmp(ident.name.c_str(), "insert")) {
      id.arrayFuncKind = PapyrusBuiltinArrayFunctionKind::Insert;
    } else if (!_stricmp(ident.name.c_str(), "remove")) {
      id.arrayFuncKind = PapyrusBuiltinArrayFunctionKind::Remove;
    } else if (!_stricmp(ident.name.c_str(), "removelast")) {
      id.arrayFuncKind = PapyrusBuiltinArrayFunctionKind::RemoveLast;
    } else {
      fatalError("Unknown function '" + ident.name + "' called on an array expression!");
    }
    return id;
  } else if (baseType.type == PapyrusType::Kind::ResolvedObject) {
    for (auto& state : baseType.resolvedObject->states) {
      for (auto& func : state->functions) {
        if (!_stricmp(func->name.c_str(), ident.name.c_str())) {
          PapyrusIdentifier id = ident;
          id.type = PapyrusIdentifierType::Function;
          id.func = func;
          return id;
        }
      }
    }

    if (baseType.resolvedObject->parentClass != PapyrusType::None()) {
      if (baseType.resolvedObject->parentClass.type != PapyrusType::Kind::ResolvedObject)
        fatalError("Something is wrong here, this should already have been resolved!");
      
      return resolveFunctionIdentifier(baseType.resolvedObject->parentClass, ident);
    }
  }

  throw std::runtime_error("Unresolved function name '" + ident.name + "'!");
}

}}