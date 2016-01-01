#include <papyrus/PapyrusType.h>

#include <boost/algorithm/string/case_conv.hpp>

#include <papyrus/PapyrusObject.h>
#include <papyrus/PapyrusStruct.h>

namespace caprica { namespace papyrus {

std::string PapyrusType::getTypeString() const {
  switch (type) {
    case Kind::None:
      return "None";
    case Kind::Bool:
      return "Bool";
    case Kind::Float:
      return "Float";
    case Kind::Int:
      return "Int";
    case Kind::String:
      return "String";
    case Kind::Var:
      return "Var";
    case Kind::Array:
      return arrayElementType->getTypeString() + "[]";
    case Kind::Unresolved:
      return name;
    case Kind::ResolvedObject:
    {
      auto lowerType = resolvedObject->name;
      boost::algorithm::to_lower(lowerType);
      return lowerType;
    }
    case Kind::ResolvedStruct:
    {
      auto name = resolvedStruct->parentObject->name + "#" + resolvedStruct->name;
      boost::algorithm::to_lower(name);
      return name;
    }
    default:
      throw std::runtime_error("Unknown PapyrusTypeKind!");
  }
}

}}