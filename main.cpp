#include <google/protobuf/compiler/importer.h>
#include <google/protobuf/descriptor.h>

#include <iostream>
#include <sstream>
#include <memory>
#include <list>

using namespace std;

using google::protobuf::Descriptor;
using google::protobuf::FieldDescriptor;
using google::protobuf::EnumDescriptor;

class ErrorCollector : public google::protobuf::compiler::MultiFileErrorCollector
{
public:
    ErrorCollector() {}

    void AddError(const std::string & filename, int line, int column, const std::string & message) override
    {
        cerr << "Error: " << filename << "@" << line << "," << column << ": " << message << endl;
    }

    void AddWarning(const std::string & filename, int line, int column, const std::string & message) override
    {
        cerr << "Warning: " << filename << "@" << line << "," << column << ": " << message << endl;
    }
};

class Source
{
    using DiskSourceTree = google::protobuf::compiler::DiskSourceTree;
    using Importer = google::protobuf::compiler::Importer;
    using DescriptorPool = google::protobuf::DescriptorPool;
    using FileDescriptor = google::protobuf::FileDescriptor;

public:
    Source() {}
    Source(const string & file_path, const string & root_dir)
    {
        source_tree.MapPath("", root_dir);

        ErrorCollector error_collector;

        importer = make_shared<Importer>(&source_tree, &error_collector);

        d_file_descriptor = importer->Import(file_path);
        if (!d_file_descriptor)
        {
            throw std::runtime_error("Failed to load source.");
        }
    }

    const FileDescriptor * file_descriptor() const { return d_file_descriptor; }
    const DescriptorPool * pool() const { return importer->pool(); }

private:
    DiskSourceTree source_tree;
    ErrorCollector error_collector;
    shared_ptr<Importer> importer;
    const FileDescriptor * d_file_descriptor = nullptr;
};

class Comparison
{
public:
    enum ItemType
    {
        Enum_Value_Id_Changed,
        Enum_Value_Added,
        Enum_Value_Removed,
        Message_Field_Name_Changed,
        Message_Field_Id_Changed,
        Message_Field_Label_Changed,
        Message_Field_Type_Changed,
        Message_Field_Default_Value_Changed,
        Message_Field_Added,
        Message_Field_Removed,
        File_Message_Added,
        File_Message_Removed,
        File_Enum_Added,
        File_Enum_Removed,
        Name_Missing
    };

    struct Item
    {
        Item(ItemType t, const string & a, const string & b): type(t), a(a), b(b) {}
        ItemType type;
        string a;
        string b;

        string message() const;
    };

    enum SectionType
    {
        Root_Section,
        Message_Comparison,
        Message_Field_Comparison,
        Enum_Comparison,
        Enum_Value_Comparison
    };

    struct Section
    {
        Section(SectionType t, const string & a, const string & b):
            type(t), a(a), b(b) {}

        SectionType type;
        string a;
        string b;

        list<Section> subsections;
        list<Item> items;

        Section & add_subsection(SectionType t, const string & a, const string & b)
        {
            subsections.emplace_back(t, a, b);
            return subsections.back();
        }

        void add_item(ItemType t, const string & a, const string & b)
        {
            items.emplace_back(t, a, b);
        }

        bool is_empty() const { return subsections.empty() and items.empty(); }

        void trim()
        {
            auto s = subsections.begin();
            while (s != subsections.end())
            {
                s->trim();
                if (s->is_empty())
                    s = subsections.erase(s);
                else
                    ++s;
            }
        }

        string message() const;

        void print(int level = 0)
        {
            cout << string(level*2, ' ') << message() << endl;

            ++level;

            string subprefix(level*2, ' ');

            for (auto & item : items)
            {
                cout << subprefix << "* " << item.message() << endl;
            }

            for (auto & subsection : subsections)
            {
                subsection.print(level);
            }
        }

    };

    void compare(Source & source1, Source & source2);
    void compare(Source & source1, Source & source2, const string & message_name);
    Section compare(const EnumDescriptor * enum1, const EnumDescriptor * enum2);
    Section compare(const Descriptor * desc1, const Descriptor * desc2);
    Section compare(const FieldDescriptor * field1, const FieldDescriptor * field2);
    bool compare_default_value(const FieldDescriptor * field1, const FieldDescriptor * field2);

    Section root { Root_Section, "", "" };
};

string Comparison::Item::message() const
{
    string msg;

    switch (type)
    {
    case Enum_Value_Id_Changed:
        msg = "Value ID changed";
        break;
    case Enum_Value_Added:
        msg = "Value added";
        break;
    case Enum_Value_Removed:
        msg = "Value removed";
        break;
    case Message_Field_Name_Changed:
        msg = "Name changed";
        break;
    case Message_Field_Id_Changed:
        msg = "ID changed";
        break;
    case Message_Field_Label_Changed:
        msg = "Label changed";
        break;
    case Message_Field_Type_Changed:
        msg = "Type changed";
        break;
    case Message_Field_Default_Value_Changed:
        msg = "Default value changed";
        break;
    case Message_Field_Added:
        msg = "Field added";
        break;
    case Message_Field_Removed:
        msg = "Field removed";
        break;
    case File_Message_Added:
        msg = "Message added";
        break;
    case File_Message_Removed:
        msg = "Message removed";
        break;
    case File_Enum_Added:
        msg = "Enum added";
        break;
    case File_Enum_Removed:
        msg = "Enum removed";
        break;
    case Name_Missing:
        msg = "Name missing";
        break;
    default:
        msg = "?";
        return msg;
    }

    msg += ": " + a + " -> " + b;

    return msg;
}

string Comparison::Section::message() const
{
    ostringstream msg;

    switch(type)
    {
    case Root_Section:
        msg << "/";
        break;
    case Message_Comparison:
        msg << "Comparing messages: " << a << " -> " << b;
        break;
    case Message_Field_Comparison:
        msg << "Comparing message fields: " << a << " -> " << b;
        break;
    case Enum_Comparison:
        msg << "Comparing enums: " << a << " -> " << b;
        break;
    case Enum_Value_Comparison:
        msg << "Comparing enum values: " << a << " -> " << b;
        break;
    default:
        msg << "?";
    }

    return msg.str();
}

void print_field(const FieldDescriptor * field)
{
    cout << field->number() << " = " << field->full_name()
         << " " << field->type_name()
         << " " << field->label();

    if (field->has_default_value())
    {
        cout << " (";
        switch(field->cpp_type())
        {
        case FieldDescriptor::CPPTYPE_INT32:
            cout << field->default_value_int32(); break;
        case FieldDescriptor::CPPTYPE_INT64:
            cout << field->default_value_int64(); break;
        case FieldDescriptor::CPPTYPE_UINT32:
            cout << field->default_value_uint32(); break;
        case FieldDescriptor::CPPTYPE_UINT64:
            cout << field->default_value_uint64(); break;
        case FieldDescriptor::CPPTYPE_FLOAT:
            cout << field->default_value_float(); break;
        case FieldDescriptor::CPPTYPE_DOUBLE:
            cout << field->default_value_double(); break;
        case FieldDescriptor::CPPTYPE_BOOL:
            cout << field->default_value_bool(); break;
        case FieldDescriptor::CPPTYPE_STRING:
            cout << field->default_value_string(); break;
        case FieldDescriptor::CPPTYPE_ENUM:
            cout << field->default_value_enum()->name(); break;
        }
        cout << " )";
    }
}

bool Comparison::compare_default_value(const FieldDescriptor * field1, const FieldDescriptor * field2)
{
    if (field1->has_default_value() != field2->has_default_value())
        return false;

    if (!field1->has_default_value())
        return true;

    if (field1->cpp_type() != field2->cpp_type())
        return false;

    switch(field1->cpp_type())
    {
    case FieldDescriptor::CPPTYPE_INT32:
        return field1->default_value_int32() == field2->default_value_int32(); break;
    case FieldDescriptor::CPPTYPE_INT64:
        return field1->default_value_int64() == field2->default_value_int64(); break;
    case FieldDescriptor::CPPTYPE_UINT32:
        return field1->default_value_uint32() == field1->default_value_uint32(); break;
    case FieldDescriptor::CPPTYPE_UINT64:
        return field1->default_value_uint64() == field2->default_value_uint64(); break;
    case FieldDescriptor::CPPTYPE_FLOAT:
        return field1->default_value_float() == field2->default_value_float(); break;
    case FieldDescriptor::CPPTYPE_DOUBLE:
        return field1->default_value_double() == field1->default_value_double(); break;
    case FieldDescriptor::CPPTYPE_BOOL:
        return field1->default_value_bool() == field1->default_value_bool(); break;
    case FieldDescriptor::CPPTYPE_STRING:
        return field1->default_value_string() == field1->default_value_string(); break;
    case FieldDescriptor::CPPTYPE_ENUM:
        return field1->default_value_enum()->number() == field1->default_value_enum()->number(); break;
    default:
        return false;
    }
}

Comparison::Section Comparison::compare(const EnumDescriptor * enum1, const EnumDescriptor * enum2)
{
    Section section(Enum_Comparison, enum1->full_name(), enum2->full_name());

    for (int i = 0; i < enum1->value_count(); ++i)
    {
        auto * value1 = enum1->value(i);
        auto * value2 = enum2->FindValueByName(value1->name());

        if (value2)
        {
            auto & subsection = section.add_subsection(Enum_Value_Comparison, value1->name(), value2->name());

            if (value1->number() != value2->number())
            {
                subsection.add_item(Enum_Value_Id_Changed,
                                    to_string(value1->number()), to_string(value2->number()));
            }
        }
        else
        {
            section.add_item(Enum_Value_Removed, value1->name(), "");
        }
    }

    for (int i = 0; i < enum2->value_count(); ++i)
    {
        auto * value2 = enum2->value(i);
        auto * value1 = enum1->FindValueByName(value2->name());
        if (!value1)
        {
            section.add_item(Enum_Value_Added, "", value2->name());
        }
    }

    return section;
}

Comparison::Section Comparison::compare(const FieldDescriptor * field1, const FieldDescriptor * field2)
{
#if 0
    print_field(field1);
    cout << " -> ";
    print_field(field2);
    cout << endl;
#endif

    Section section(Message_Field_Comparison, field1->full_name(), field2->full_name());

    if (field1->name() != field2->name())
    {
        section.add_item(Message_Field_Name_Changed, field1->name(), field2->name());
    }

    if (field1->number() != field2->number())
    {
        section.add_item(Message_Field_Id_Changed, to_string(field1->number()), to_string(field2->number()));
    }

    if (field1->label() != field2->label())
    {
        section.add_item(Message_Field_Label_Changed, "", "");
    }

    if (field1->type() != field2->type())
    {
        section.add_item(Message_Field_Type_Changed, field1->type_name(), field2->type_name());
    }
    else if (field1->type() == FieldDescriptor::TYPE_ENUM)
    {
        auto * enum1 = field1->enum_type();
        auto * enum2 = field2->enum_type();

        if (enum1->full_name() != enum2->full_name())
        {
            section.add_item(Message_Field_Type_Changed, enum1->full_name(), enum2->full_name());
        }

        {
            section.subsections.push_back(compare(enum1, enum2));
        }
    }
    else if (field1->type() == FieldDescriptor::TYPE_MESSAGE)
    {
        auto * msg1 = field1->message_type();
        auto * msg2 = field2->message_type();

        if (msg1->full_name() != msg2->full_name())
        {
            section.add_item(Message_Field_Type_Changed, msg1->full_name(), msg2->full_name());
        }

        {
            section.subsections.push_back(compare(msg1, msg2));
        }
    }

    if (field1->cpp_type() == field2->cpp_type())
    {
        if (!compare_default_value(field1, field2))
        {
            section.add_item(Message_Field_Default_Value_Changed, "", "");
        }
    }

    return section;
}

Comparison::Section Comparison::compare(const Descriptor * desc1, const Descriptor * desc2)
{
    Section section(Message_Comparison, desc1->full_name(), desc2->full_name());

    for (int i = 0; i < desc1->field_count(); ++i)
    {
        auto * field1 = desc1->field(i);
        auto * field2 = desc2->FindFieldByName(field1->name());

        if (field2)
        {
            section.subsections.push_back(compare(field1, field2));
        }
        else
        {
            section.add_item(Message_Field_Removed, field1->name(), "");
        }
    }

    for (int i = 0; i < desc2->field_count(); ++i)
    {
        auto * field2 = desc2->field(i);
        auto * field1 = desc1->FindFieldByName(field2->name());
        if (!field1)
        {
            section.add_item(Message_Field_Added, "", field2->name());
        }
    }

    return section;

#if 0
    //
    std::set<int> field_number_set;

    for (int i = 0; i < desc1.field_count(); ++i)
    {
        field_number_set.insert(desc1.field(i)->number());
    }

    for (int i = 0; i < desc2.field_count(); ++i)
    {
        field_number_set.insert(desc2.field(i)->number());
    }

    for (int field_number : field_number_set)
    {
        auto field1 = desc1->FindFieldByNumber(field_number);
        auto field2 = desc2->FindFieldByNumber(field_number);
        if (!field1)
        {
            cout << desc2->full_name() << ": added field id: " << field_number << endl;
            continue;
        }
        if (!field2)
        {
            cout << desc2->full_name() << ": removed field id: " << field_number << endl;
            continue;
        }
        compare(field1, field2);
    }
#endif
}

void Comparison::compare(Source & source1, Source & source2)
{
    auto * file1 = source1.file_descriptor();
    auto * file2 = source2.file_descriptor();

    for (int i = 0; i < file1->message_type_count(); ++i)
    {
        auto * msg1 = file1->message_type(i);
        auto * msg2 = file2->FindMessageTypeByName(msg1->name());
        if (msg2)
        {
            root.subsections.push_back(compare(msg1, msg2));
        }
        else
        {
            root.add_item(File_Message_Removed, msg1->full_name(), "");
        }
    }

    for (int i = 0; i < file2->message_type_count(); ++i)
    {
        auto * msg2 = file2->message_type(i);
        auto * msg1 = file1->FindMessageTypeByName(msg2->name());
        if (!msg1)
        {
            root.add_item(File_Message_Added, " ", msg2->full_name());
        }
    }

    for (int i = 0; i < file1->enum_type_count(); ++i)
    {
        auto * enum1 = file1->enum_type(i);
        auto * enum2 = file2->FindEnumTypeByName(enum1->name());
        if (enum2)
        {
            root.subsections.push_back(compare(enum1, enum2));
        }
        else
        {
            root.add_item(File_Enum_Removed, enum1->full_name(), "");
        }
    }

    for (int i = 0; i < file2->enum_type_count(); ++i)
    {
        auto * enum2 = file2->enum_type(i);
        auto * enum1 = file1->FindEnumTypeByName(enum2->name());
        if (!enum1)
        {
            root.add_item(File_Enum_Added, "", enum2->full_name());
        }
    }
}

void Comparison::compare(Source & source1, Source & source2, const string & message_or_enum_name)
{
    auto desc1 = source1.pool()->FindMessageTypeByName(message_or_enum_name);
    auto desc2 = source2.pool()->FindMessageTypeByName(message_or_enum_name);

    auto enum1 = source1.pool()->FindEnumTypeByName(message_or_enum_name);
    auto enum2 = source2.pool()->FindEnumTypeByName(message_or_enum_name);

    if (desc1 and desc2)
    {
        root.subsections.push_back(compare(desc1, desc2));
    }
    else if (enum1 and enum2)
    {
        root.subsections.push_back(compare(enum1, enum2));
    }
    else
    {
        root.add_item(Name_Missing, message_or_enum_name, message_or_enum_name);
    }
}

int main(int argc, char * argv[])
{
    if (argc < 6)
    {
        cerr << "Expected arguments: root-dir1 file1 root-dir2 file2 message" << endl;
        cerr << "Use '.' for message to compare all messages in given files." << endl;
        return 1;
    }

    Comparison comparison;

    try
    {
        Source source1(argv[2], argv[1]);
        Source source2(argv[4], argv[3]);
        string message_name = argv[5];
        if (message_name == ".")
            comparison.compare(source1, source2);
        else
            comparison.compare(source1, source2, message_name);
    }
    catch(std::exception & e)
    {
        cerr << e.what() << endl;
        return 1;
    }

    comparison.root.trim();
    comparison.root.print();

    return 0;
}

