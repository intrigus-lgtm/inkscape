#define __SP_REPR_IO_C__

/*
 * Dirty DOM-like  tree
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *
 * Copyright (C) 1999-2002 Lauris Kaplinski
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <strings.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include <glib.h>

#include "xml/repr.h"
#include "xml/attribute-record.h"

#include "io/sys.h"
#include "io/inkscapestream.h"
#include "io/uristream.h"
#include "io/gzipstream.h"

#include <map>
#include <glibmm/ustring.h>
#include <glibmm/quark.h>
#include "util/list.h"
#include "util/shared-c-string-ptr.h"

using Inkscape::IO::Writer;
using Inkscape::Util::List;
using Inkscape::Util::cons;
using Inkscape::XML::Document;
using Inkscape::XML::Node;
using Inkscape::XML::AttributeRecord;

static Document *sp_repr_do_read (xmlDocPtr doc, const gchar *default_ns);
static Node *sp_repr_svg_read_node (xmlNodePtr node, const gchar *default_ns, GHashTable *prefix_map);
static gint sp_repr_qualified_name (gchar *p, gint len, xmlNsPtr ns, const xmlChar *name, const gchar *default_ns, GHashTable *prefix_map);
static void sp_repr_write_stream_root_element (Node *repr, Writer &out, gboolean add_whitespace, gchar const *default_ns);
static void sp_repr_write_stream (Node *repr, Writer &out, gint indent_level, gboolean add_whitespace, Glib::QueryQuark elide_prefix);
static void sp_repr_write_stream_element (Node *repr, Writer &out, gint indent_level, gboolean add_whitespace, Glib::QueryQuark elide_prefix, List<AttributeRecord const> attributes);

#ifdef HAVE_LIBWMF
static xmlDocPtr sp_wmf_convert (const char * file_name);
static char * sp_wmf_image_name (void * context);
#endif /* HAVE_LIBWMF */


class XmlSource
{
public:
    XmlSource()
        : filename(0),
          fp(0),
          first(false),
          dummy("x"),
          instr(0),
          gzin(0)
    {
    }
    virtual ~XmlSource()
    {
        close();
    }

    void setFile( char const * filename );

    static int readCb( void * context, char * buffer, int len);
    static int closeCb(void * context);


    int read( char * buffer, int len );
    int close();
private:
    const char* filename;
    FILE* fp;
    bool first;
    Inkscape::URI dummy;
    Inkscape::IO::UriInputStream* instr;
    Inkscape::IO::GzipInputStream* gzin;
};

void XmlSource::setFile( char const * filename ) {
    this->filename = filename;
    fp = Inkscape::IO::fopen_utf8name(filename, "r");
    first = true;
}


int XmlSource::readCb( void * context, char * buffer, int len )
{
    int retVal = -1;
    if ( context ) {
        XmlSource* self = static_cast<XmlSource*>(context);
        retVal = self->read( buffer, len );
    }
    return retVal;
}

int XmlSource::closeCb(void * context)
{
    if ( context ) {
        XmlSource* self = static_cast<XmlSource*>(context);
        self->close();
    }
    return 0;
}

int XmlSource::read( char *buffer, int len )
{
    int retVal = 0;
    size_t got = 0;

    if ( first ) {
        first = false;
        char tmp[] = {0,0};
        size_t some = fread( tmp, 1, 2, fp );

        if ( (some >= 2) && (tmp[0] == 0x1f) && ((unsigned char)(tmp[1]) == 0x8b) ) {
            //g_message(" the file being read is gzip'd. extract it");
            fclose(fp);
            fp = 0;
            fp = Inkscape::IO::fopen_utf8name(filename, "r");
            instr = new Inkscape::IO::UriInputStream(fp, dummy);
            gzin = new Inkscape::IO::GzipInputStream(*instr);
            int single = 0;
            while ( (int)got < len && single >= 0 )
            {
                single = gzin->get();
                if ( single >= 0 ) {
                    buffer[got++] = 0x0ff & single;
                } else {
                    break;
                }
            }
            //g_message(" extracted %d bytes this pass", got );
        } else {
            memcpy( buffer, tmp, some );
            got = some;
        }
    } else if ( gzin ) {
        int single = 0;
        while ( (int)got < len && single >= 0 )
        {
            single = gzin->get();
            if ( single >= 0 ) {
                buffer[got++] = 0x0ff & single;
            } else {
                break;
            }
        }
        //g_message(" extracted %d bytes this pass  b", got );
    } else {
        got = fread( buffer, 1, len, fp );
    }

    if ( feof(fp) ) {
        retVal = got;
    }
    else if ( ferror(fp) ) {
        retVal = -1;
    }
    else {
        retVal = got;
    }

    return retVal;
}

int XmlSource::close()
{
    if ( gzin ) {
        gzin->close();
        delete gzin;
        gzin = 0;
    }
    if ( instr ) {
        instr->close();
        fp = 0;
        delete instr;
        instr = 0;
    }
    if ( fp ) {
        fclose(fp);
        fp = 0;
    }
    return 0;
}

/**
 * Reads XML from a file, including WMF files, and returns the Document.
 * The default namespace can also be specified, if desired.
 */
Document *
sp_repr_read_file (const gchar * filename, const gchar *default_ns)
{
    xmlDocPtr doc = 0;
    Document * rdoc = 0;

    xmlSubstituteEntitiesDefault(1);

    g_return_val_if_fail (filename != NULL, NULL);
    g_return_val_if_fail (Inkscape::IO::file_test( filename, G_FILE_TEST_EXISTS ), NULL);

    // TODO: bulia, please look over
    gsize bytesRead = 0;
    gsize bytesWritten = 0;
    GError* error = NULL;
    // TODO: need to replace with our own fopen and reading
    gchar* localFilename = g_filename_from_utf8 ( filename,
                                 -1,  &bytesRead,  &bytesWritten, &error);
    g_return_val_if_fail( localFilename != NULL, NULL );

    Inkscape::IO::dump_fopen_call( filename, "N" );

    XmlSource src;
    src.setFile(filename);

    xmlDocPtr doubleDoc = xmlReadIO( XmlSource::readCb,
                                     XmlSource::closeCb,
                                     &src,
                                     localFilename,
                                     NULL, //"UTF-8",
                                     0 );


#ifdef HAVE_LIBWMF
    if (strlen (localFilename) > 4) {
        if ( (strcmp (localFilename + strlen (localFilename) - 4,".wmf") == 0)
          || (strcmp (localFilename + strlen (localFilename) - 4,".WMF") == 0))
            doc = sp_wmf_convert (localFilename);
        else
            doc = xmlParseFile (localFilename);
    }
    else {
        doc = xmlParseFile (localFilename);
    }
#else /* !HAVE_LIBWMF */
    //doc = xmlParseFile (localFilename);
#endif /* !HAVE_LIBWMF */

    //rdoc = sp_repr_do_read (doc, default_ns);
    rdoc = sp_repr_do_read (doubleDoc, default_ns);
    if (doc)
        xmlFreeDoc (doc);

    if ( localFilename != NULL )
        g_free (localFilename);

    if ( doubleDoc != NULL )
    {
        xmlFreeDoc( doubleDoc );
    }

    return rdoc;
}

/**
 * Reads and parses XML from a buffer, returning it as an Document
 */
Document *
sp_repr_read_mem (const gchar * buffer, gint length, const gchar *default_ns)
{
    xmlDocPtr doc;
    Document * rdoc;

    xmlSubstituteEntitiesDefault(1);

    g_return_val_if_fail (buffer != NULL, NULL);

    doc = xmlParseMemory ((gchar *) buffer, length);

    rdoc = sp_repr_do_read (doc, default_ns);
    if (doc)
        xmlFreeDoc (doc);
    return rdoc;
}

namespace Inkscape {

struct compare_quark_ids {
    bool operator()(Glib::QueryQuark const &a, Glib::QueryQuark const &b) {
        return a.id() < b.id();
    }
};

}

namespace {

typedef std::map<Glib::QueryQuark, Glib::QueryQuark, Inkscape::compare_quark_ids> PrefixMap;

Glib::QueryQuark qname_prefix(Glib::QueryQuark qname) {
    static PrefixMap prefix_map;
    PrefixMap::iterator iter = prefix_map.find(qname);
    if ( iter != prefix_map.end() ) {
        return (*iter).second;
    } else {
        gchar const *name_string=g_quark_to_string(qname);
        gchar const *prefix_end=strchr(name_string, ':');
        if (prefix_end) {
            Glib::Quark prefix=Glib::ustring(name_string, prefix_end);
            prefix_map.insert(PrefixMap::value_type(qname, prefix));
            return prefix;
        } else {
            return GQuark(0);
        }
    }
}

}

namespace {

void promote_to_svg_namespace(Node *repr) {
    if ( repr->type() == Inkscape::XML::ELEMENT_NODE ) {
        GQuark code = repr->code();
        if (!qname_prefix(code).id()) {
            gchar *svg_name = g_strconcat("svg:", g_quark_to_string(code), NULL);
            repr->setCodeUnsafe(g_quark_from_string(svg_name));
            g_free(svg_name);
        }
        for ( Node *child = sp_repr_children(repr) ; child ; child = sp_repr_next(child) ) {
            promote_to_svg_namespace(child);
        }
    }
}

}

/**
 * Reads in a XML file to create a Document
 */
Document *
sp_repr_do_read (xmlDocPtr doc, const gchar *default_ns)
{
    if (doc == NULL) return NULL;
    xmlNodePtr node=xmlDocGetRootElement (doc);
    if (node == NULL) return NULL;

    GHashTable * prefix_map;
    prefix_map = g_hash_table_new (g_str_hash, g_str_equal);

    GSList *reprs=NULL;
    Node *root=NULL;

    for ( node = doc->children ; node != NULL ; node = node->next ) {
        if (node->type == XML_ELEMENT_NODE) {
            Node *repr=sp_repr_svg_read_node (node, default_ns, prefix_map);
            reprs = g_slist_append(reprs, repr);

            if (!root) {
                root = repr;
            } else {
                root = NULL;
                break;
            }
        } else if ( node->type == XML_COMMENT_NODE ) {
            Node *comment=sp_repr_svg_read_node(node, default_ns, prefix_map);
            reprs = g_slist_append(reprs, comment);
        }
    }

    Document *rdoc=NULL;

    if (root != NULL) {
        /* promote elements of SVG documents that don't use namespaces
         * into the SVG namespace */
        if ( default_ns && !strcmp(default_ns, SP_SVG_NS_URI)
             && !strcmp(root->name(), "svg") )
        {
            promote_to_svg_namespace(root);
        }

        rdoc = sp_repr_document_new_list(reprs);
    }

    for ( GSList *iter = reprs ; iter ; iter = iter->next ) {
        Node *repr=(Node *)iter->data;
        sp_repr_unref(repr);
    }
    g_slist_free(reprs);

    g_hash_table_destroy (prefix_map);

    return rdoc;
}

gint
sp_repr_qualified_name (gchar *p, gint len, xmlNsPtr ns, const xmlChar *name, const gchar *default_ns, GHashTable *prefix_map)
{
    const xmlChar *prefix;
    if ( ns && ns->href ) {
        prefix = (xmlChar*)sp_xml_ns_uri_prefix ((gchar*)ns->href, (char*)ns->prefix);
        g_hash_table_insert (prefix_map, (gpointer)prefix, (gpointer)ns->href);
    } else {
        prefix = NULL;
    }

    if (prefix)
        return g_snprintf (p, len, "%s:%s", (gchar*)prefix, name);
    else
        return g_snprintf (p, len, "%s", name);
}

static Node *
sp_repr_svg_read_node (xmlNodePtr node, const gchar *default_ns, GHashTable *prefix_map)
{
    Node *repr, *crepr;
    xmlAttrPtr prop;
    xmlNodePtr child;
    gchar c[256];

    if (node->type == XML_TEXT_NODE || node->type == XML_CDATA_SECTION_NODE) {

        if (node->content == NULL || *(node->content) == '\0')
            return NULL; // empty text node

        bool preserve = (xmlNodeGetSpacePreserve (node) == 1);

        xmlChar *p;
        for (p = node->content; *p && g_ascii_isspace (*p) && !preserve; p++)
            ; // skip all whitespace

        if (!(*p)) { // this is an all-whitespace node, and preserve == default
            return NULL; // we do not preserve all-whitespace nodes unless we are asked to
        }

        Node *rdoc = sp_repr_new_text((const gchar *)node->content);
        return rdoc;
    }

    if (node->type == XML_COMMENT_NODE)
        return sp_repr_new_comment((const gchar *)node->content);

    if (node->type == XML_ENTITY_DECL) return NULL;

    sp_repr_qualified_name (c, 256, node->ns, node->name, default_ns, prefix_map);
    repr = sp_repr_new (c);
    /* TODO remember node->ns->prefix if node->ns != NULL */

    for (prop = node->properties; prop != NULL; prop = prop->next) {
        if (prop->children) {
            sp_repr_qualified_name (c, 256, prop->ns, prop->name, default_ns, prefix_map);
            sp_repr_set_attr (repr, c, (gchar*)prop->children->content);
            /* TODO remember prop->ns->prefix if prop->ns != NULL */
        }
    }

    if (node->content)
        repr->setContent((gchar*)node->content);

    child = node->xmlChildrenNode;
    for (child = node->xmlChildrenNode; child != NULL; child = child->next) {
        crepr = sp_repr_svg_read_node (child, default_ns, prefix_map);
        if (crepr) {
            repr->appendChild(crepr);
            sp_repr_unref (crepr);
        }
    }

    return repr;
}

void
sp_repr_save_stream (Document *doc, FILE *fp, gchar const *default_ns, bool compress)
{
    Node *repr;
    const gchar *str;

    Inkscape::URI dummy("x");
    Inkscape::IO::UriOutputStream bout(fp, dummy);
    Inkscape::IO::GzipOutputStream *gout = compress ? new Inkscape::IO::GzipOutputStream(bout) : NULL;
    Inkscape::IO::OutputStreamWriter *out  = compress ? new Inkscape::IO::OutputStreamWriter( *gout ) : new Inkscape::IO::OutputStreamWriter( bout );

    /* fixme: do this The Right Way */

    out->writeString( "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n" );

    str = ((Node *)doc)->attribute("doctype");
    if (str) {
        out->writeString( str );
    }

    repr = sp_repr_document_first_child(doc);
    for ( repr = sp_repr_document_first_child(doc) ;
          repr ; repr = sp_repr_next(repr) )
    {
        if ( repr->type() == Inkscape::XML::ELEMENT_NODE ) {
            sp_repr_write_stream_root_element(repr, *out, TRUE, default_ns);
        } else if ( repr->type() == Inkscape::XML::COMMENT_NODE ) {
            sp_repr_write_stream(repr, *out, 0, TRUE, GQuark(0));
            out->writeChar( '\n' );
        } else {
            sp_repr_write_stream(repr, *out, 0, TRUE, GQuark(0));
        }
    }
    if ( out ) {
        delete out;
        out = NULL;
    }
    if ( gout ) {
        delete gout;
        gout = NULL;
    }
}

/* Returns TRUE if file successfully saved; FALSE if not
 */
gboolean
sp_repr_save_file (Document *doc, const gchar *filename,
                   gchar const *default_ns)
{
    if (filename == NULL) {
        return FALSE;
    }
    bool compress = false;
    {
        if (strlen (filename) > 5) {
            gchar tmp[] = {0,0,0,0,0,0};
            strncpy( tmp, filename + strlen (filename) - 5, 6 );
            tmp[5] = 0;
            if ( strcasecmp(".svgz", tmp ) == 0 )
            {
                //g_message("TIME TO COMPRESS THE OUTPUT FOR SVGZ");
                compress = true;
            }
        }
    }

    Inkscape::IO::dump_fopen_call( filename, "B" );
    FILE *file = Inkscape::IO::fopen_utf8name(filename, "w");
    if (file == NULL) {
        return FALSE;
    }

    sp_repr_save_stream (doc, file, default_ns, compress);

    if (fclose (file) != 0) {
        return FALSE;
    }

    return TRUE;
}

void
sp_repr_print (Node * repr)
{
    Inkscape::IO::StdOutputStream bout;
    Inkscape::IO::OutputStreamWriter out(bout);

    sp_repr_write_stream (repr, out, 0, TRUE, GQuark(0));

    return;
}

/* (No doubt this function already exists elsewhere.) */
static void
repr_quote_write (Writer &out, const gchar * val)
{
    if (!val) return;

    for (; *val != '\0'; val++) {
        switch (*val) {
        case '"': out.writeString( "&quot;" ); break;
        case '&': out.writeString( "&amp;" ); break;
        case '<': out.writeString( "&lt;" ); break;
        case '>': out.writeString( "&gt;" ); break;
        default: out.writeChar( *val ); break;
        }
    }
}

namespace {

typedef std::map<Glib::QueryQuark, gchar const *, Inkscape::compare_quark_ids> LocalNameMap;
typedef std::map<Glib::QueryQuark, Inkscape::Util::SharedCStringPtr, Inkscape::compare_quark_ids> NSMap;

gchar const *qname_local_name(Glib::QueryQuark qname) {
    static LocalNameMap local_name_map;
    LocalNameMap::iterator iter = local_name_map.find(qname);
    if ( iter != local_name_map.end() ) {
        return (*iter).second;
    } else {
        gchar const *name_string=g_quark_to_string(qname);
        gchar const *prefix_end=strchr(name_string, ':');
        if (prefix_end) {
            return prefix_end + 1;
        } else {
            return name_string;
        }
    }
}

void add_ns_map_entry(NSMap &ns_map, Glib::QueryQuark prefix) {
    using Inkscape::Util::SharedCStringPtr;

    static const Glib::QueryQuark xml_prefix("xml");

    NSMap::iterator iter=ns_map.find(prefix);
    if ( iter == ns_map.end() ) {
        if (prefix.id()) {
            gchar const *uri=sp_xml_ns_prefix_uri(g_quark_to_string(prefix));
            if (uri) {
                ns_map.insert(NSMap::value_type(prefix, SharedCStringPtr::coerce(uri)));
            } else if ( prefix != xml_prefix ) {
                g_warning("No namespace known for normalized prefix %s", g_quark_to_string(prefix));
            }
        } else {
            ns_map.insert(NSMap::value_type(prefix, SharedCStringPtr()));
        }
    }
}

void populate_ns_map(NSMap &ns_map, Node &repr) {
    if ( repr.type() == Inkscape::XML::ELEMENT_NODE ) {
        add_ns_map_entry(ns_map, qname_prefix(repr.code()));
        for ( List<AttributeRecord const> iter=repr.attributeList() ;
              iter ; ++iter )
        {
            Glib::QueryQuark prefix=qname_prefix(iter->key);
            if (prefix.id()) {
                add_ns_map_entry(ns_map, prefix);
            }
        }
        for ( Node *child=sp_repr_children(&repr) ;
              child ; child = sp_repr_next(child) )
        {
            populate_ns_map(ns_map, *child);
        }
    }
}

}

void
sp_repr_write_stream_root_element (Node *repr, Writer &out, gboolean add_whitespace, gchar const *default_ns)
{
    using Inkscape::Util::SharedCStringPtr;
    g_assert(repr != NULL);

    NSMap ns_map;
    populate_ns_map(ns_map, *repr);

    Glib::QueryQuark elide_prefix=GQuark(0);
    if ( default_ns && ns_map.find(GQuark(0)) == ns_map.end() ) {
        elide_prefix = g_quark_from_string(sp_xml_ns_uri_prefix(default_ns, NULL));
    }

    List<AttributeRecord const> attributes=repr->attributeList();
    for ( NSMap::iterator iter=ns_map.begin() ; iter != ns_map.end() ; ++iter ) 
    {
        Glib::QueryQuark prefix=(*iter).first;
        SharedCStringPtr ns_uri=(*iter).second;

        if (prefix.id()) {
            if ( elide_prefix == prefix ) {
                attributes = cons(AttributeRecord(g_quark_from_static_string("xmlns"), ns_uri), attributes);
            }

            Glib::ustring attr_name="xmlns:";
            attr_name.append(g_quark_to_string(prefix));
            GQuark key = g_quark_from_string(attr_name.c_str());
            attributes = cons(AttributeRecord(key, ns_uri), attributes);
        } else {
            // if there are non-namespaced elements, we can't globally
            // use a default namespace
            elide_prefix = GQuark(0);
        }
    }

    return sp_repr_write_stream_element(repr, out, 0, add_whitespace, elide_prefix, attributes);
}

void
sp_repr_write_stream (Node *repr, Writer &out, gint indent_level,
                      gboolean add_whitespace, Glib::QueryQuark elide_prefix)
{
    if (repr->type() == Inkscape::XML::TEXT_NODE) {
        repr_quote_write (out, repr->content());
    } else if (repr->type() == Inkscape::XML::COMMENT_NODE) {
        out.printf( "<!--%s-->", repr->content() );
    } else if (repr->type() == Inkscape::XML::ELEMENT_NODE) {
        sp_repr_write_stream_element(repr, out, indent_level, add_whitespace, elide_prefix, repr->attributeList());
    } else {
        g_assert_not_reached();
    }
}

void
sp_repr_write_stream_element (Node * repr, Writer & out, gint indent_level,
                              gboolean add_whitespace,
                              Glib::QueryQuark elide_prefix,
                              List<AttributeRecord const> attributes)
{
    Node *child;
    gboolean loose;
    gint i;

    g_return_if_fail (repr != NULL);

    if ( indent_level > 16 )
        indent_level = 16;

    if (add_whitespace) {
        for ( i = 0 ; i < indent_level ; i++ ) {
            out.writeString( "  " );
        }
    }

    GQuark code = repr->code();
    gchar const *element_name;
    if ( elide_prefix == qname_prefix(code) ) {
        element_name = qname_local_name(code);
    } else {
        element_name = g_quark_to_string(code);
    }
    out.printf( "<%s", element_name );

    // TODO: this should depend on xml:space, not the element name

    // if this is a <text> element, suppress formatting whitespace
    // for its content and children:

    if (!strcmp(repr->name(), "svg:text")) {
        add_whitespace = FALSE;
    }

    for ( List<AttributeRecord const> iter = attributes ;
          iter ; ++iter )
    {
        out.writeString("\n");
        for ( i = 0 ; i < indent_level + 1 ; i++ ) {
            out.writeString("  ");
        }
        out.printf(" %s=\"", g_quark_to_string(iter->key));
        repr_quote_write(out, iter->value);
        out.writeChar('"');
    }

    loose = TRUE;
    for (child = repr->firstChild() ; child != NULL; child = child->next()) {
        if (child->type() == Inkscape::XML::TEXT_NODE) {
            loose = FALSE;
            break;
        }
    }
    if (repr->firstChild()) {
        out.writeString( ">" );
        if (loose && add_whitespace) {
            out.writeString( "\n" );
        }
        for (child = repr->firstChild(); child != NULL; child = child->next()) {
            sp_repr_write_stream (child, out, (loose) ? (indent_level + 1) : 0, add_whitespace, elide_prefix);
        }

        if (loose && add_whitespace) {
            for (i = 0; i < indent_level; i++) {
                out.writeString( "  " );
            }
        }
        out.printf( "</%s>", element_name );
    } else {
        out.writeString( " />" );
    }

    // text elements cannot nest, so we can output newline
    // after closing text

    if (add_whitespace || !strcmp (repr->name(), "svg:text")) {
        out.writeString( "\n" );
    }
}


/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=99 :
