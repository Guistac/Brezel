#pragma once
#include <sstream>
#include <string>
#include <vector>
#include <functional>

#include "Brezel/Console/SearchVisitor.hpp"
#include "Brezel/Core/Project.hpp"
#include "Brezel/Foundation/Signal.hpp"

namespace Brezel {

class PropertyCollectorVisitor : public ComponentVisitor {
public:
  std::vector<std::string> properties;
  std::vector<std::string> actions;

  virtual void visit_property(StringID label, int &val,
                              std::initializer_list<Tag> tags = {}) override {
    properties.push_back(label.toString());
  }
  virtual void visit_property(StringID label, float &val,
                              std::initializer_list<Tag> tags = {}) override {
    properties.push_back(label.toString());
  }
  virtual void visit_property(StringID label, bool &val,
                              std::initializer_list<Tag> tags = {}) override {
    properties.push_back(label.toString());
  }
  virtual void visit_property(StringID label, std::string &val,
                              std::initializer_list<Tag> tags = {}) override {
    properties.push_back(label.toString());
  }
  virtual void visit_property(StringID label, StringID &val,
                              std::initializer_list<Tag> tags = {}) override {
    properties.push_back(label.toString());
  }
  virtual void visit_property(StringID label, ParameterBase &p,
                              std::initializer_list<Tag> tags = {}) override {
    properties.push_back(label.toString());
  }
  virtual void visit_property(StringID label, EntityReference &ref,
                              std::initializer_list<Tag> tags = {}) override {
    properties.push_back(label.toString());
  }
  virtual void visit_action(StringID label,
                            std::function<void()> fn) override {
    actions.push_back(label.toString());
  }
};

class Console {
public:
  Console(Project &project) : m_project(project) {}

  Signal<std::string> onOutput;

  std::vector<std::string> parse_args(std::string_view line) {
    std::vector<std::string> args;
    std::string current;
    bool in_quotes = false;
    for (char c : line) {
      if (c == '"') {
        in_quotes = !in_quotes;
      } else if (std::isspace(c) && !in_quotes) {
        if (!current.empty()) {
          args.push_back(current);
          current.clear();
        }
      } else {
        current += c;
      }
    }
    if (!current.empty()) {
      args.push_back(current);
    }
    return args;
  }

  std::vector<std::string> completeVerb(std::string_view prefix) {
    std::vector<std::string> matches;
    const char *verbs[] = {"get", "set", "call", "ls", "undo", "exit", "quit"};
    for (const char *v : verbs) {
      if (std::string_view(v).starts_with(prefix)) {
        matches.push_back(v);
      }
    }
    return matches;
  }

  std::vector<std::string> completePath(std::string_view prefix, std::string_view verb) {
    std::vector<std::string> matches;

    auto lastDot = prefix.find_last_of('.');
    if (lastDot == std::string_view::npos) {
      auto lastSlash = prefix.find_last_of('/');
      std::string_view parentPath =
          (lastSlash == std::string_view::npos) ? "" : prefix.substr(0, lastSlash);
      std::string_view childPrefix = (lastSlash == std::string_view::npos)
                                    ? prefix
                                    : prefix.substr(lastSlash + 1);

      auto addEntityMatches = [&](Entity entity, const std::string &pathSoFar,
                                  const std::string &name) {
        bool hasChildren = !entity.get<HierarchyComponent>().children.empty();
        bool hasComponents = false;
        if (verb != "ls") {
            for (auto &[cname, info] : ComponentRegistry::componentInfoByTypeName) {
              if (info.hasComponent(entity.handle())) {
                hasComponents = true;
                break;
              }
            }
        }
        std::string fullPath =
            pathSoFar.empty() ? name : (pathSoFar + "/" + name);
        if (hasChildren)
          matches.push_back(fullPath + "/");
        if (hasComponents)
          matches.push_back(fullPath + ".");
        if (!hasChildren && !hasComponents)
          matches.push_back(fullPath);
      };

      if (parentPath.empty()) {
        auto view = m_project.getRegistry()
                        .view<HierarchyComponent, IdentityComponent>();
        for (auto e : view) {
          if (!view.get<HierarchyComponent>(e).parent) {
            std::string_view name = view.get<IdentityComponent>(e).name;
            if (name.starts_with(childPrefix)) {
              addEntityMatches(Entity(e, m_project.getRegistry()), "", std::string(name));
            }
          }
        }
      } else {
        Entity parent = m_project.getEntityByPath(parentPath);
        if (parent) {
          auto &hier = parent.get<HierarchyComponent>();
          for (auto child : hier.children) {
            if (child.has<IdentityComponent>()) {
              std::string_view name = child.get<IdentityComponent>().name;
              if (name.starts_with(childPrefix)) {
                addEntityMatches(child, std::string(parentPath), std::string(name));
              }
            }
          }
        }
      }
    } else {
      if (verb == "ls") return matches;

      auto secondLastDot = prefix.find_last_of('.', lastDot - 1);
      if (secondLastDot == std::string_view::npos) {
        std::string_view entityPath = prefix.substr(0, lastDot);
        std::string_view compPrefix = prefix.substr(lastDot + 1);

        Entity entity = m_project.getEntityByPath(entityPath);
        if (entity) {
          for (auto &[name, info] :
               ComponentRegistry::componentInfoByTypeName) {
            if (info.hasComponent(entity.handle())) {
              std::string nameStr = name.toString();
              if (nameStr.starts_with(compPrefix)) {
                matches.push_back(std::string(entityPath) + "." + nameStr + ".");
              }
            }
          }
        }
      } else {
        std::string_view entityPath = prefix.substr(0, secondLastDot);
        std::string_view compName =
            prefix.substr(secondLastDot + 1, lastDot - secondLastDot - 1);
        std::string_view propPrefix = prefix.substr(lastDot + 1);

        Entity entity = m_project.getEntityByPath(entityPath);
        if (entity) {
          PropertyCollectorVisitor visitor;
          if (ComponentRegistry::reflectEntityComponent(
                  entity.handle(), StringID::from(std::string(compName)), visitor)) {
            const auto& targets = (verb == "call") ? visitor.actions : visitor.properties;
            for (const auto &pName : targets) {
              if (pName.starts_with(propPrefix)) {
                matches.push_back(std::string(entityPath) + "." + std::string(compName) + "." + pName);
              }
            }
          }
        }
      }
    }
    return matches;
  }

  void exec(std::string_view line) {
    onOutput.emit(std::string(line));

    auto args = parse_args(line);
    if (args.empty())
      return;

    std::string verb = args[0];

    if (verb == "undo") {
      m_project.getStack().undo();
      onOutput.emit("[OK] Undid last command.");
      return;
    }

    if (args.size() < 2) {
      onOutput.emit("[ERROR] Missing path argument.");
      return;
    }

    std::string path = args[1];

    if (verb == "ls") {
      handle_ls(path);
      return;
    }

    auto lastDot = path.find_last_of('.');
    if (lastDot == std::string::npos) {
      onOutput.emit("[ERROR] Invalid path format. Expected Entity.Component.Property");
      return;
    }
    auto secondLastDot = path.find_last_of('.', lastDot - 1);
    if (secondLastDot == std::string::npos) {
      onOutput.emit("[ERROR] Invalid path format. Expected Entity.Component.Property");
      return;
    }

    std::string entityPath = path.substr(0, secondLastDot);
    std::string compName = path.substr(secondLastDot + 1, lastDot - secondLastDot - 1);
    std::string propName = path.substr(lastDot + 1);

    Entity entity = m_project.getEntityByPath(entityPath);
    if (!entity) {
      onOutput.emit("[ERROR] Unknown entity '" + entityPath + "'");
      return;
    }

    SearchVisitor visitor(propName);
    if (!ComponentRegistry::reflectEntityComponent(entity.handle(), StringID::from(compName), visitor)) {
      onOutput.emit("[ERROR] Component '" + compName + "' not found on entity '" + entityPath + "'");
      return;
    }

    if (!visitor.isFound()) {
      onOutput.emit("[ERROR] Property or action '" + propName + "' not found on component '" + compName + "'");
      return;
    }

    auto result = visitor.getResult();

    if (verb == "get") {
      handle_get(result, path);
    } else if (verb == "call") {
      if (result.type != SearchVisitor::FoundType::Action) {
        onOutput.emit("[ERROR] '" + path + "' is not an action.");
        return;
      }
      if (result.action) {
        result.action();
        onOutput.emit("[OK] Executed " + path);
      }
    } else if (verb == "set") {
      if (args.size() < 3) {
        onOutput.emit("[ERROR] Missing value argument for set.");
        return;
      }
      std::string valueStr = args[2];
      handle_set(result, path, valueStr);
    } else {
      onOutput.emit("[ERROR] Unknown command verb '" + verb + "'");
    }
  }

private:
  Project &m_project;

  void handle_ls(const std::string &path) {
    Entity entity = m_project.getEntityByPath(path);
    if (!entity) {
      onOutput.emit("[ERROR] Unknown entity '" + path + "'");
      return;
    }

    std::string out = "[OK] Components on '" + path + "':\n";
    bool foundAny = false;
    for (auto &[name, info] : ComponentRegistry::componentInfoByTypeName) {
      if (info.hasComponent(entity.handle())) {
        out += "  - " + name.toString() + "\n";
        foundAny = true;
      }
    }
    if (!foundAny)
      out += "  (none)\n";

    auto &hier = entity.get<HierarchyComponent>();
    if (!hier.children.empty()) {
      out += "Children:\n";
      for (auto child : hier.children) {
        if (child.has<IdentityComponent>()) {
          auto &id = child.get<IdentityComponent>();
          out += "  - " + id.name + " (\"" + id.displayName + "\")\n";
        }
      }
    }
    onOutput.emit(out);
  }

  void handle_get(const SearchVisitor::FoundResult &result, const std::string &path) {
    std::string out = "[OK] " + path + " = ";
    switch (result.type) {
    case SearchVisitor::FoundType::Int:
      out += std::to_string(*static_cast<int *>(result.ptr));
      break;
    case SearchVisitor::FoundType::Float:
      out += std::to_string(*static_cast<float *>(result.ptr));
      break;
    case SearchVisitor::FoundType::Bool:
      out += (*static_cast<bool *>(result.ptr) ? "true" : "false");
      break;
    case SearchVisitor::FoundType::String:
      out += *static_cast<std::string *>(result.ptr);
      break;
    case SearchVisitor::FoundType::ParameterBase:
      out += static_cast<ParameterBase *>(result.ptr)->toString();
      break;
    case SearchVisitor::FoundType::Action:
      out = "[ERROR] Cannot 'get' an action. Use 'call'.";
      break;
    default:
      out = "[ERROR] Unknown type.";
      break;
    }
    onOutput.emit(out);
  }

  void handle_set(const SearchVisitor::FoundResult &result, const std::string &path, const std::string &valueStr) {
    try {
      switch (result.type) {
      case SearchVisitor::FoundType::Int: {
        int v = std::stoi(valueStr);
        *static_cast<int *>(result.ptr) = v;
        onOutput.emit("[OK] Set " + path + " = " + std::to_string(v));
        break;
      }
      case SearchVisitor::FoundType::Float: {
        float v = std::stof(valueStr);
        *static_cast<float *>(result.ptr) = v;
        onOutput.emit("[OK] Set " + path + " = " + std::to_string(v));
        break;
      }
      case SearchVisitor::FoundType::Bool: {
        bool v = (valueStr == "true" || valueStr == "1");
        *static_cast<bool *>(result.ptr) = v;
        onOutput.emit("[OK] Set " + path + " = " + (v ? "true" : "false"));
        break;
      }
      case SearchVisitor::FoundType::String: {
        *static_cast<std::string *>(result.ptr) = valueStr;
        onOutput.emit("[OK] Set " + path + " = " + valueStr);
        break;
      }
      case SearchVisitor::FoundType::ParameterBase: {
        static_cast<ParameterBase *>(result.ptr)->fromString(valueStr);
        onOutput.emit("[OK] Set " + path + " = " + valueStr);
        break;
      }
      case SearchVisitor::FoundType::Action: {
        onOutput.emit("[ERROR] Cannot 'set' an action.");
        break;
      }
      default:
        onOutput.emit("[ERROR] Unknown type.");
        break;
      }
    } catch (const std::exception &e) {
      onOutput.emit(std::string("[ERROR] Type conversion failed: ") + e.what());
    }
  }
};

} // namespace Brezel
