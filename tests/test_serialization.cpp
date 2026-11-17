// SPDX-License-Identifier: MIT
// Brezel tests. Copyright (c) 2026 Leo Becker. See LICENSE.md.
//
// Serialization: the XML round trip (entities, hierarchy, components, lists, entity references), the XML layout, what
// the loader reports for a field or a component that is unknown or missing, the relink pass, files; the binary codec
// round trip and layout, its filters, and hostile input: a count larger than the payload, a payload cut short, a
// component whose reflect reads less than was written, reading on after an error (start plan section 4 row 10;
// 09_BREZEL.md B-01, B-02, B-22).
// Headers: Serialization/XmlSerializer.hpp, Serialization/XmlDeserializer.hpp, Serialization/XmlCommon.hpp,
// Serialization/EntityReferenceLinker.hpp, Serialization/DeserializationValidation.hpp,
// Serialization/BinaryVisitor.hpp.

#include "BrezelTest.hpp"
#include "TestComponents.hpp"

#include "Brezel/Serialization/BinaryVisitor.hpp"
#include "Brezel/Serialization/DeserializationValidation.hpp"
#include "Brezel/Serialization/EntityReferenceLinker.hpp"
#include "Brezel/Serialization/XmlCommon.hpp"
#include "Brezel/Serialization/XmlDeserializer.hpp"
#include "Brezel/Serialization/XmlSerializer.hpp"

#include <tinyxml2.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace Brezel;
using namespace BrezelTest;

namespace {

std::string saveToString(Project& project) {
    tinyxml2::XMLDocument doc;
    tinyxml2::XMLElement* root = doc.NewElement(Xml::projectTagString);
    doc.InsertEndChild(root);
    root->SetAttribute("Name", "Saved");
    CHECK(Xml::saveProjectToNode(project, root, &doc));
    tinyxml2::XMLPrinter printer;
    doc.Print(&printer);
    return printer.CStr();
}

bool loadFromString(Project& project, const std::string& xml, DeserializationValidationReport& report) {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS) return false;
    return Xml::loadProjectFromNode(project, doc.FirstChildElement(Xml::projectTagString), report);
}

bool reportHas(DeserializationValidationReport& report, Severity severity, const std::string& text) {
    for (const auto& e : report.getErrors()) {
        if (e.severity == severity && e.message.find(text) != std::string::npos) return true;
    }
    return false;
}

template <typename T>
void encodeInto(BinarySerializer& s, T& component, const char* saveString) {
    if (s.beginComponent(StringID::from(saveString))) {
        reflect(component, s);
        s.endComponent();
    }
}

template <typename T>
bool decodeFrom(BinaryDeserializer& d, T& component, const char* saveString) {
    if (!d.beginComponent(StringID::from(saveString))) return false;
    reflect(component, d);
    d.endComponent();
    return true;
}

template <typename T>
T readAt(const std::vector<uint8_t>& buffer, size_t offset) {
    T value{};
    std::memcpy(&value, buffer.data() + offset, sizeof(T));
    return value;
}

/// An older reflect of Position that reads x and y only (build skew)
struct PositionV1 {
    float x{0.0f};
    float y{0.0f};
};
template <typename V>
void reflect(PositionV1& p, V& v) {
    v.visit_property("x", p.x, {Tag::Persistent, Tag::Networked});
    v.visit_property("y", p.y, {Tag::Persistent, Tag::Networked});
}

std::string xmlEntity(const char* attributes, const char* body) {
    return std::string("<Project Name=\"P\"><Entity ") + attributes + ">" + body + "<Children/></Entity></Project>";
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// XML
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(xml_round_trip, "Serialization/XmlSerializer.hpp",
            "a project saved to XML and loaded into another project comes back with the same UUIDs, names, display "
            "names, hierarchy, component values and entity references (after relink); untagged fields and Transient "
            "components are not saved; the report is Valid") {
    registerTestComponents();
    TestWorld source("Source", false);
    Entity root = source.project.createEntityWithUUID(UUID(1001), "Root Thing");
    Entity child = source.project.createEntityWithUUID(UUID(1002), "Child");
    Entity other = source.project.createEntityWithUUID(UUID(1003), "Other");
    child.setParent(root);
    root.add<Position>(1.5f, -2.0f, 3);
    root.add<Label>(Label{"hello world", false, StringID::from("alpha"), "not saved"});
    root.add<Scratch>(Scratch{9});
    child.add<Link>().target.set(other);
    other.add<Nested>(Nested{5, Inner{6, 7.5f}});
    const std::string xml = saveToString(source.project);

    TestWorld target("Target", false);
    DeserializationValidationReport report;
    REQUIRE(loadFromString(target.project, xml, report));
    relinkProjectReferences(target.project, report);
    CHECK(report.getSeverity() == Severity::Valid);
    for (const auto& e : report.getErrors()) std::printf("    report: %s\n", e.message.c_str());

    CHECK_EQ(target.project.getEntities().size(), size_t(3));
    Entity r = target.project.getEntityByUUID(UUID(1001));
    Entity c = target.project.getEntityByUUID(UUID(1002));
    Entity o = target.project.getEntityByUUID(UUID(1003));
    REQUIRE(r.isValid() && c.isValid() && o.isValid());
    CHECK_EQ(r.getName(), std::string_view("Root_Thing"));
    CHECK_EQ(r.getDisplayName(), std::string_view("Root Thing"));
    CHECK(c.getParent() == r);
    REQUIRE(r.has<Position>());
    CHECK_EQ(r.get<Position>().x, 1.5f);
    CHECK_EQ(r.get<Position>().y, -2.0f);
    CHECK_EQ(r.get<Position>().layer, 3);
    REQUIRE(r.has<Label>());
    CHECK_EQ(r.get<Label>().text, std::string("hello world"));
    CHECK(!r.get<Label>().visible);
    CHECK(r.get<Label>().kind == StringID::from("alpha"));
    CHECK(r.get<Label>().note.empty());
    CHECK(!r.has<Scratch>());
    REQUIRE(c.has<Link>());
    CHECK(c.get<Link>().target.uuid == UUID(1003));
    CHECK(c.get<Link>().target.entity == o);
    REQUIRE(o.has<Nested>());
    CHECK_EQ(o.get<Nested>().id, 5);
    CHECK((o.get<Nested>().inner == Inner{6, 7.5f}));
}

BREZEL_TEST(xml_layout, "Serialization/XmlSerializer.hpp",
            "the XML layout: <Entity Name DisplayName UUID>, one element per saved component named by its save "
            "string, one element per Persistent property with the value in val, a list with ListSize and E0, E1, "
            "..., an entity reference as a UUID attribute, then <Children>; no IdentityComponent element") {
    registerTestComponents();
    TestWorld world("Layout", false);
    Entity e = world.project.createEntityWithUUID(UUID(1101), "E");
    Entity child = world.project.createEntityWithUUID(UUID(1102), "C");
    child.setParent(e);
    e.add<Position>(1.5f, 2.0f, 3);
    e.add<Lists>(Lists{{4, 5}, {}, {}});
    e.add<Link>().target.set(child);
    e.add<Scratch>();
    tinyxml2::XMLDocument doc;
    REQUIRE(doc.Parse(saveToString(world.project).c_str()) == tinyxml2::XML_SUCCESS);
    tinyxml2::XMLElement* ent = doc.FirstChildElement("Project")->FirstChildElement("Entity");
    REQUIRE(ent != nullptr);
    CHECK_EQ(std::string(ent->Attribute("Name")), std::string("E"));
    CHECK_EQ(ent->Unsigned64Attribute("UUID"), uint64_t(1101));
    REQUIRE(ent->FirstChildElement("Position") != nullptr);
    CHECK_EQ(ent->FirstChildElement("Position")->FirstChildElement("x")->FloatAttribute("val"), 1.5f);
    CHECK_EQ(ent->FirstChildElement("Position")->FirstChildElement("layer")->IntAttribute("val"), 3);
    tinyxml2::XMLElement* numbers = ent->FirstChildElement("Lists")->FirstChildElement("numbers");
    REQUIRE(numbers != nullptr);
    CHECK_EQ(numbers->IntAttribute("ListSize"), 2);
    CHECK_EQ(numbers->FirstChildElement("E1")->IntAttribute("val"), 5);
    CHECK_EQ(ent->FirstChildElement("Link")->FirstChildElement("target")->Unsigned64Attribute("UUID"), uint64_t(1102));
    CHECK(ent->FirstChildElement("IdentityComponent") == nullptr);
    CHECK(ent->FirstChildElement("Scratch") == nullptr);
    CHECK(ent->LastChildElement() == ent->FirstChildElement("Children"));
    CHECK(ent->FirstChildElement("Children")->FirstChildElement("Entity") != nullptr);
}

BREZEL_TEST(known_defect_BUG155_xml_plain_list_values_lost, "Serialization/XmlDeserializer.hpp",
            "a list of plain values or strings is saved with its values but loads with the right size and default "
            "values: the loader visits list elements without tags and skips untagged properties (BUG-155)") {
    registerTestComponents();
    TestWorld source("Source", false);
    Entity e = source.project.createEntityWithUUID(UUID(1201), "E");
    e.add<Lists>(Lists{{4, 5, 6}, {"p", "q"}, {}});
    const std::string xml = saveToString(source.project);
    CHECK(xml.find("val=\"6\"") != std::string::npos);  // the values are in the file
    TestWorld target("Target", false);
    DeserializationValidationReport report;
    REQUIRE(loadFromString(target.project, xml, report));
    Entity loaded = target.project.getEntityByUUID(UUID(1201));
    REQUIRE(loaded.has<Lists>());
    CHECK((loaded.get<Lists>().numbers == std::vector<int>{0, 0, 0}));
    CHECK((loaded.get<Lists>().words == std::vector<std::string>{"", ""}));
}

BREZEL_TEST(known_defect_new1_xml_struct_list_elements, "Serialization/XmlSerializer.hpp",
            "a list of structs is saved without E0, E1 wrappers (the element's fields go straight under the list "
            "element), so the loader reads the first element's fields for every element (new finding)") {
    registerTestComponents();
    TestWorld source("Source", false);
    Entity e = source.project.createEntityWithUUID(UUID(1301), "E");
    e.add<Lists>(Lists{{}, {}, {Inner{1, 1.5f}, Inner{2, 2.5f}}});
    const std::string xml = saveToString(source.project);
    tinyxml2::XMLDocument doc;
    REQUIRE(doc.Parse(xml.c_str()) == tinyxml2::XML_SUCCESS);
    tinyxml2::XMLElement* items =
        doc.FirstChildElement("Project")->FirstChildElement("Entity")->FirstChildElement("Lists")->FirstChildElement("items");
    REQUIRE(items != nullptr);
    CHECK(items->FirstChildElement("E0") == nullptr);
    int fieldElements = 0;
    for (auto* f = items->FirstChildElement(); f; f = f->NextSiblingElement()) ++fieldElements;
    CHECK_EQ(fieldElements, 4);  // a, b, a, b
    TestWorld target("Target", false);
    DeserializationValidationReport report;
    REQUIRE(loadFromString(target.project, xml, report));
    const auto& loaded = target.project.getEntityByUUID(UUID(1301)).get<Lists>().items;
    REQUIRE(loaded.size() == 2);
    CHECK((loaded[0] == Inner{1, 1.5f}));
    CHECK((loaded[1] == Inner{1, 1.5f}));  // the second element is lost
}

BREZEL_TEST(xml_missing_and_unknown_fields, "Serialization/XmlDeserializer.hpp",
            "a missing property keeps the component's default and adds a Warning; an unknown property element is "
            "ignored without a report entry; an unknown component element adds a Warning and the entity still loads; "
            "loading a component runs no hook") {
    registerTestComponents();
    int created = 0;
    ComponentRegistry::registerComponent<Position>("Position", {.flags = ComponentRegistry::ComponentFlag::Networked,
                                                                .onUpdated = [&created](Entity) { ++created; },
                                                                .onCreated = [&created](Entity) { ++created; }});
    TestWorld world("Load", false);
    DeserializationValidationReport report;
    const std::string xml = xmlEntity("Name=\"E\" DisplayName=\"E\" UUID=\"1401\"",
                                      "<Position><x val=\"2.5\"/><layer val=\"4\"/><zzz val=\"1\"/></Position>"
                                      "<Mystery><q val=\"1\"/></Mystery>");
    REQUIRE(loadFromString(world.project, xml, report));
    Entity e = world.project.getEntityByUUID(UUID(1401));
    REQUIRE(e.has<Position>());
    CHECK_EQ(e.get<Position>().x, 2.5f);
    CHECK_EQ(e.get<Position>().y, 0.0f);
    CHECK_EQ(e.get<Position>().layer, 4);
    CHECK(report.getSeverity() == Severity::Warning);
    CHECK(reportHas(report, Severity::Warning, "float attribute \"y\""));
    CHECK(reportHas(report, Severity::Warning, "Component Type does not exist in registry"));
    CHECK_EQ(report.getErrors().size(), size_t(2));
    CHECK_EQ(created, 0);
    registerTestComponents();
}

BREZEL_TEST(xml_identity_errors, "Serialization/XmlDeserializer.hpp",
            "an entity without a UUID or with UUID 0 is a Critical and is still created with UUID 0; a name that "
            "sanitizing changes, a missing name and a missing display name are Warnings") {
    TestWorld world("Load", false);
    DeserializationValidationReport noUuid;
    REQUIRE(loadFromString(world.project, xmlEntity("Name=\"A\" DisplayName=\"A\"", ""), noUuid));
    CHECK(noUuid.getSeverity() == Severity::Critical);
    CHECK(reportHas(noUuid, Severity::Critical, "Error resolving entity UUID"));
    CHECK(world.project.getEntityByUUID(UUID(0)).isValid());

    DeserializationValidationReport zero;
    REQUIRE(loadFromString(world.project, xmlEntity("Name=\"Z\" DisplayName=\"Z\" UUID=\"0\"", ""), zero));
    CHECK(reportHas(zero, Severity::Critical, "Invalid UUID value"));

    DeserializationValidationReport names;
    REQUIRE(loadFromString(world.project, xmlEntity("Name=\"a b\" UUID=\"1501\"", ""), names));
    CHECK(names.getSeverity() == Severity::Warning);
    CHECK(reportHas(names, Severity::Warning, "sanitized from 'a b' to 'a_b'"));
    CHECK(reportHas(names, Severity::Warning, "Error resolving entity DisplayName"));
    CHECK_EQ(world.project.getEntityByUUID(UUID(1501)).getName(), std::string_view("a_b"));

    DeserializationValidationReport noName;
    REQUIRE(loadFromString(world.project, xmlEntity("DisplayName=\"D\" UUID=\"1502\"", ""), noName));
    CHECK(reportHas(noName, Severity::Warning, "Error resolving entity Name"));
    CHECK_EQ(world.project.getEntityByUUID(UUID(1502)).getName(), std::string_view("Entity"));
}

BREZEL_TEST(xml_list_size_errors, "Serialization/XmlDeserializer.hpp",
            "a list element without a valid ListSize, or with a negative one, is a Critical and leaves the list as "
            "it was; a missing list element is a Warning") {
    registerTestComponents();
    TestWorld world("Load", false);
    DeserializationValidationReport report;
    const std::string xml = xmlEntity("Name=\"E\" DisplayName=\"E\" UUID=\"1601\"",
                                      "<Lists><numbers><E0 val=\"1\"/></numbers><words ListSize=\"-1\"/></Lists>");
    REQUIRE(loadFromString(world.project, xml, report));
    Entity e = world.project.getEntityByUUID(UUID(1601));
    REQUIRE(e.has<Lists>());
    CHECK(e.get<Lists>().numbers.empty());
    CHECK(e.get<Lists>().words.empty());
    CHECK(report.getSeverity() == Severity::Critical);
    CHECK(reportHas(report, Severity::Critical, "Error resolving size of list attribute 'numbers'"));
    CHECK(reportHas(report, Severity::Critical, "is negative: -1"));
    CHECK(reportHas(report, Severity::Warning, "list attribute 'items'"));
}

BREZEL_TEST(xml_relink_references, "Serialization/EntityReferenceLinker.hpp",
            "relinkProjectReferences sets each reference's handle from the UUID index, on children too, and adds a "
            "Warning for a UUID it cannot find (the handle stays invalid)") {
    registerTestComponents();
    TestWorld world("Relink", false);
    DeserializationValidationReport report;
    const std::string xml =
        "<Project Name=\"P\">"
        "<Entity Name=\"T\" DisplayName=\"T\" UUID=\"1701\"><Children/></Entity>"
        "<Entity Name=\"P\" DisplayName=\"P\" UUID=\"1702\"><Children>"
        "  <Entity Name=\"C\" DisplayName=\"C\" UUID=\"1703\"><Link><target UUID=\"1701\"/></Link><Children/></Entity>"
        "  <Entity Name=\"D\" DisplayName=\"D\" UUID=\"1704\"><Link><target UUID=\"9999\"/></Link><Children/></Entity>"
        "</Children></Entity></Project>";
    REQUIRE(loadFromString(world.project, xml, report));
    Entity c = world.project.getEntityByUUID(UUID(1703));
    Entity d = world.project.getEntityByUUID(UUID(1704));
    CHECK(!c.get<Link>().target.entity.isValid());  // the loader stores the UUID only
    relinkProjectReferences(world.project, report);
    CHECK(c.get<Link>().target.entity == world.project.getEntityByUUID(UUID(1701)));
    CHECK(!d.get<Link>().target.entity.isValid());
    CHECK(d.get<Link>().target.uuid == UUID(9999));
    CHECK(reportHas(report, Severity::Warning, "Error linking EntityReference \"target\""));
}

BREZEL_TEST(xml_project_file, "Serialization/XmlSerializer.hpp",
            "saveProject writes a <Project Name> file and loadProject reads it into a project, adding to what it "
            "holds and taking the name; a file that cannot be read returns false") {
    registerTestComponents();
    const auto path = std::filesystem::temp_directory_path() /
                      ("brezel_test_" + std::to_string(UUIDProvider::generate().value) + ".xml");
    {
        TestWorld source("Saved Name", false);
        Entity e = source.project.createEntityWithUUID(UUID(1801), "E");
        e.add<Position>(1.0f, 2.0f, 3);
        CHECK(Xml::saveProject(source.project, path.string()));
    }
    TestWorld target("Before", false);
    target.project.createEntityWithUUID(UUID(1802), "Kept");
    DeserializationValidationReport report;
    CHECK(Xml::loadProject(target.project, path.string(), report));
    CHECK_EQ(target.project.getName(), std::string_view("Saved Name"));
    CHECK(target.project.getEntityByUUID(UUID(1801)).has<Position>());
    CHECK(target.project.getEntityByUUID(UUID(1802)).isValid());
    std::filesystem::remove(path);
    DeserializationValidationReport missing;
    CHECK(!Xml::loadProject(target.project, path.string(), missing));
}

BREZEL_TEST(validation_report, "Serialization/DeserializationValidation.hpp",
            "the report keeps every error in order, its severity is the worst one, and an error's path joins the "
            "entity path with a separator") {
    DeserializationValidationReport report;
    CHECK(report.getSeverity() == Severity::Valid);
    report.addError(Severity::Warning, {"a", "b"}, "Comp", "first");
    report.addError(Severity::Critical, {"a"}, "Comp", "second");
    report.addError(Severity::Warning, {}, "", "third");
    CHECK(report.getSeverity() == Severity::Critical);
    REQUIRE(report.getErrors().size() == 3);
    CHECK_EQ(report.getErrors()[1].message, std::string("second"));
    ValidationError e = report.getErrors()[0];
    CHECK_EQ(e.getPath("/"), std::string("a/b"));
    CHECK_EQ(report.getErrors()[2].entityPath.size(), size_t(0));
}

// ---------------------------------------------------------------------------------------------------------------------
// Binary
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(binary_round_trip_all_types, "Serialization/BinaryVisitor.hpp",
            "four components of every field type in one buffer decode back to equal values with the All filter; the "
            "decoder reads the whole buffer; an entity reference carries its UUID only and keeps the target's handle") {
    registerTestComponents();
    TestWorld world("Binary", false);
    Entity sentinel = world.project.createEntity("sentinel");
    Position p{1.25f, -3.5f, 42};
    Label l{"text \xC3\xA9", false, StringID::from("beta"), "note"};
    Lists lists{{1, -2, 3}, {"x", "", "zz"}, {Inner{7, 0.5f}}};
    Link link;
    link.target.uuid = UUID(0x1122334455667788ULL);
    BinarySerializer s;
    encodeInto(s, p, "Position");
    encodeInto(s, l, "Label");
    encodeInto(s, lists, "Lists");
    encodeInto(s, link, "Link");

    Position p2;
    Label l2;
    Lists lists2;
    Link link2;
    link2.target.entity = sentinel;
    BinaryDeserializer d(s.data(), s.size());
    CHECK(decodeFrom(d, p2, "Position"));
    CHECK(decodeFrom(d, l2, "Label"));
    CHECK(decodeFrom(d, lists2, "Lists"));
    CHECK(decodeFrom(d, link2, "Link"));
    CHECK(!d.hasError());
    CHECK(d.isValid());
    CHECK_EQ(d.bytesRead(), s.size());
    CHECK_EQ(d.bytesRemaining(), size_t(0));
    CHECK_EQ(p2.x, p.x);
    CHECK_EQ(p2.y, p.y);
    CHECK_EQ(p2.layer, 42);
    CHECK_EQ(l2.text, l.text);
    CHECK(!l2.visible);
    CHECK(l2.kind == l.kind);
    CHECK_EQ(l2.note, std::string("note"));  // All: untagged fields too
    CHECK((lists2.numbers == lists.numbers));
    CHECK((lists2.words == lists.words));
    CHECK((lists2.items == lists.items));
    CHECK(link2.target.uuid == link.target.uuid);
    CHECK(link2.target.entity == sentinel);
}

BREZEL_TEST(binary_layout, "Serialization/BinaryVisitor.hpp",
            "per component: the save string's 64-bit hash, a 32-bit payload length back-patched at the end, then the "
            "properties in reflect order (int 32-bit, float raw, bool one byte, string length then bytes, StringID 64 "
            "bits), in the machine's byte order; any non-zero bool byte decodes as true") {
    Position p{1.0f, 2.0f, 7};
    BinarySerializer s;
    encodeInto(s, p, "Position");
    const auto& b = s.getBuffer();
    REQUIRE(b.size() == 8 + 4 + 12);
    CHECK_EQ(readAt<uint64_t>(b, 0), StringID::hash_string_view("Position"));
    CHECK_EQ(readAt<uint32_t>(b, 8), uint32_t(12));
    CHECK_EQ(readAt<float>(b, 12), 1.0f);
    CHECK_EQ(readAt<float>(b, 16), 2.0f);
    CHECK_EQ(readAt<int32_t>(b, 20), 7);

    Label l{"ab", true, StringID::from("k"), ""};
    BinarySerializer ls;
    encodeInto(ls, l, "Label");
    auto lb = ls.takeBuffer();
    REQUIRE(lb.size() == 12 + (4 + 2) + 1 + 8 + 4);
    CHECK_EQ(readAt<uint32_t>(lb, 12), uint32_t(2));
    CHECK_EQ(lb[16], uint8_t('a'));
    CHECK_EQ(lb[18], uint8_t(1));
    CHECK_EQ(readAt<uint64_t>(lb, 19), StringID::from("k").hash());
    lb[18] = 7;
    Label decoded;
    decoded.visible = false;
    BinaryDeserializer d(lb.data(), lb.size());
    CHECK(decodeFrom(d, decoded, "Label"));
    CHECK(decoded.visible);
}

BREZEL_TEST(binary_filters, "Serialization/BinaryVisitor.hpp",
            "NetworkedOnly encodes only Networked properties and skips a whole registered type without the "
            "Networked flag; PersistentOnly encodes only Persistent properties and skips a Transient type; an "
            "unregistered type is always encoded; a decoder with the same filter restores what was sent") {
    registerTestComponents();
    Position p{1.0f, 2.0f, 3};
    BinarySerializer net(BinaryPropertyFilter::NetworkedOnly);
    encodeInto(net, p, "Position");
    CHECK_EQ(net.size(), size_t(12 + 8));  // x and y; layer is not networked
    Position back{0.0f, 0.0f, 99};
    BinaryDeserializer d(net.data(), net.size(), BinaryPropertyFilter::NetworkedOnly);
    CHECK(decodeFrom(d, back, "Position"));
    CHECK_EQ(back.x, 1.0f);
    CHECK_EQ(back.layer, 99);
    CHECK(!d.hasError());

    Nested n{1, Inner{2, 3.0f}};
    BinarySerializer netNested(BinaryPropertyFilter::NetworkedOnly);
    encodeInto(netNested, n, "Nested");  // registered without the Networked flag
    CHECK_EQ(netNested.size(), size_t(0));

    Scratch sc{4};
    BinarySerializer persistent(BinaryPropertyFilter::PersistentOnly);
    encodeInto(persistent, sc, "Scratch");  // Transient
    CHECK_EQ(persistent.size(), size_t(0));
    Label l{"t", true, StringID::from("k"), "untagged"};
    encodeInto(persistent, l, "Label");
    CHECK_EQ(persistent.size(), size_t(12 + (4 + 1) + 1 + 8));  // text, visible, kind; not note

    Unregistered u{5};
    BinarySerializer netUnregistered(BinaryPropertyFilter::NetworkedOnly);
    encodeInto(netUnregistered, u, "Unregistered");
    CHECK_EQ(netUnregistered.size(), size_t(12 + 4));
    netUnregistered.clear();
    CHECK_EQ(netUnregistered.size(), size_t(0));
}

BREZEL_TEST(binary_mismatched_component_skipped, "Serialization/BinaryVisitor.hpp",
            "a decoder asked for another component than the one in the buffer skips that component's payload by its "
            "length and returns false without an error; a length past the end of the buffer is an error, after which "
            "every further component is refused without reading") {
    registerTestComponents();
    Label l{"skip me", true, StringID::from("k"), ""};
    Position p{3.0f, 4.0f, 5};
    BinarySerializer s;
    encodeInto(s, l, "Label");
    encodeInto(s, p, "Position");
    BinaryDeserializer d(s.data(), s.size());
    Position back;
    CHECK(!decodeFrom(d, back, "Position"));  // the Label is skipped
    CHECK(!d.hasError());
    CHECK(decodeFrom(d, back, "Position"));
    CHECK_EQ(back.layer, 5);
    CHECK(!d.hasError());

    std::vector<uint8_t> bad = s.getBuffer();
    const uint32_t huge = 1u << 30;
    std::memcpy(bad.data() + 8, &huge, sizeof(huge));
    BinaryDeserializer badDecoder(bad.data(), bad.size());
    CHECK(!decodeFrom(badDecoder, back, "Position"));
    CHECK(badDecoder.hasError());
    // once the error is set the decoder refuses every further component without reading (review R-A F-2)
    const size_t at = badDecoder.bytesRead();
    CHECK_EQ(at, size_t(12));
    Position after{0.0f, 0.0f, 0};
    Label afterLabel;
    CHECK(!decodeFrom(badDecoder, after, "Position"));
    CHECK(!decodeFrom(badDecoder, afterLabel, "Label"));
    CHECK_EQ(badDecoder.bytesRead(), at);
    CHECK_EQ(after.layer, 0);
}

BREZEL_TEST(binary_payload_cut_short, "Serialization/BinaryVisitor.hpp",
            "a buffer cut at any length is decoded without reading past its end and sets the error; the fields "
            "decoded before the cut stay written (the payload has no string directly before a list: see "
            "known_defect_new3_decoder_reads_after_error)") {
    registerTestComponents();
    Label label{"label text", false, StringID::from("k"), "n"};
    Position position{1.0f, 2.0f, 3};
    Lists lists{{1, 2, 3}, {}, {Inner{4, 5.0f}, Inner{6, 7.0f}}};
    BinarySerializer s;
    encodeInto(s, label, "Label");
    encodeInto(s, position, "Position");
    encodeInto(s, lists, "Lists");
    const std::vector<uint8_t> full = s.takeBuffer();
    for (size_t cut = 0; cut < full.size(); ++cut) {
        std::vector<uint8_t> shortBuffer(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(cut));
        BinaryDeserializer d(shortBuffer.data(), shortBuffer.size());
        Label l2;
        Position p2;
        Lists lists2;
        const bool labelBegun = decodeFrom(d, l2, "Label");
        if (!d.hasError()) decodeFrom(d, p2, "Position");
        if (!d.hasError()) decodeFrom(d, lists2, "Lists");
        if (!d.hasError()) BrezelTest::fail(__FILE__, __LINE__, "no error at cut " + std::to_string(cut));
        CHECK(d.bytesRead() <= cut);
        CHECK(lists2.numbers.size() <= 3);
        CHECK(lists2.items.size() <= 2);
        if (cut == 12 + 4 + 10 + 2) {  // inside the label's kind: text and visible were decoded
            CHECK(labelBegun);
            CHECK_EQ(l2.text, std::string("label text"));
        }
    }
}

BREZEL_TEST(known_defect_new3_decoder_reads_after_error, "Serialization/BinaryVisitor.hpp",
            "after an error the decoder goes on: only beginComponent checks the error flag, so the next field is "
            "read from where the failed one stopped; a string whose length check fails leaves its bytes to be read "
            "as the next value, and a list count read that way is resized at once (B-01: in a first version of "
            "binary_payload_cut_short, the text of a cut string became a count of 1.7 billion elements) (new finding)") {
    std::vector<uint8_t> payload;
    const uint32_t claimed = 10;  // a string claiming 10 bytes, of which 4 are there
    const int32_t next = 2;
    payload.resize(8);
    std::memcpy(payload.data(), &claimed, 4);
    std::memcpy(payload.data() + 4, &next, 4);
    BinaryDeserializer d(payload.data(), payload.size());
    std::string text = "kept";
    std::vector<int> list;
    VectorAccessor<int> listAccess(list);
    d.visit_property(StringID::from("text"), text, {});
    CHECK(d.hasError());
    CHECK_EQ(text, std::string("kept"));
    d.visit_property(StringID::from("list"), listAccess, {});  // reads the string's bytes as the count
    CHECK_EQ(list.size(), size_t(2));
    CHECK(d.hasError());
}

BREZEL_TEST(known_defect_B01_list_count_before_check, "Serialization/BinaryVisitor.hpp",
            "the list decoder resizes the list to the 32-bit count from the wire before checking it against the bytes "
            "left: a payload of 16 bytes makes the receiver allocate a list of a million elements (09_BREZEL.md "
            "B-01)") {
    registerTestComponents();
    std::vector<uint8_t> hostile;
    const uint64_t hash = StringID::hash_string_view("Lists");
    const uint32_t length = 4;
    const uint32_t count = 1u << 20;
    hostile.resize(16);
    std::memcpy(hostile.data(), &hash, 8);
    std::memcpy(hostile.data() + 8, &length, 4);
    std::memcpy(hostile.data() + 12, &count, 4);
    Lists target;
    BinaryDeserializer d(hostile.data(), hostile.size());
    CHECK(decodeFrom(d, target, "Lists"));
    CHECK(d.hasError());
    CHECK_EQ(target.numbers.size(), size_t(count));  // allocated before the check
}

BREZEL_TEST(binary_string_length_checked, "Serialization/BinaryVisitor.hpp",
            "a string length larger than the bytes left is an error and leaves the string as it was (checked before "
            "the resize, unlike a list)") {
    std::vector<uint8_t> payload;
    const uint32_t length = 1000;
    payload.resize(4 + 3);
    std::memcpy(payload.data(), &length, 4);
    std::string value = "unchanged";
    BinaryDeserializer d(payload.data(), payload.size());
    d.visit_property(StringID::from("s"), value, {});
    CHECK(d.hasError());
    CHECK_EQ(value, std::string("unchanged"));
}

BREZEL_TEST(known_defect_B22_short_reflect_misaligns, "Serialization/BinaryVisitor.hpp",
            "when the type matches, the decoder ignores the component's length: a reflect that reads fewer fields "
            "than were written leaves the cursor inside the component and the next component is not decoded; and the "
            "encoder writes nothing for a filtered-out registered type while the decoder expects its header "
            "(09_BREZEL.md B-22)") {
    registerTestComponents();
    Position p{1.0f, 2.0f, 3};
    Label l{"next", true, StringID::from("k"), ""};
    BinarySerializer s;
    encodeInto(s, p, "Position");
    encodeInto(s, l, "Label");
    BinaryDeserializer d(s.data(), s.size());
    PositionV1 old;
    CHECK(decodeFrom(d, old, "Position"));
    CHECK_EQ(old.y, 2.0f);
    CHECK_EQ(d.bytesRead(), size_t(12 + 8));  // stopped before layer: the length was not used to skip it
    Label l2;
    CHECK(!decodeFrom(d, l2, "Label"));
    CHECK(l2.text.empty());

    Nested n{1, Inner{2, 3.0f}};
    BinarySerializer net(BinaryPropertyFilter::NetworkedOnly);
    encodeInto(net, n, "Nested");    // not networked: nothing written
    encodeInto(net, p, "Position");
    BinaryDeserializer nd(net.data(), net.size(), BinaryPropertyFilter::NetworkedOnly);
    Nested n2;
    Position p2{0.0f, 0.0f, 0};
    CHECK(!decodeFrom(nd, n2, "Nested"));   // reads Position's header, skips Position
    CHECK(!decodeFrom(nd, p2, "Position")); // nothing left
    CHECK(nd.hasError());
    CHECK_EQ(p2.x, 0.0f);
}

BREZEL_TEST_MAIN("serialization")
