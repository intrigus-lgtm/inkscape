/** \file
 * SVG displacement map filter effect
 *//*
 * Authors:
 *   Hugo Rodrigues <haa.rodrigues@gmail.com>
 *
 * Copyright (C) 2006 Hugo Rodrigues
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#ifndef SP_FEDISPLACEMENTMAP_H_SEEN
#define SP_FEDISPLACEMENTMAP_H_SEEN

#include "sp-filter-primitive.h"

#define SP_TYPE_FEDISPLACEMENTMAP (sp_feDisplacementMap_get_type())
#define SP_FEDISPLACEMENTMAP(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), SP_TYPE_FEDISPLACEMENTMAP, SPFeDisplacementMap))
#define SP_FEDISPLACEMENTMAP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), SP_TYPE_FEDISPLACEMENTMAP, SPFeDisplacementMapClass))
#define SP_IS_FEDISPLACEMENTMAP(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), SP_TYPE_FEDISPLACEMENTMAP))
#define SP_IS_FEDISPLACEMENTMAP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), SP_TYPE_FEDISPLACEMENTMAP))

enum FilterDisplacementMapChannelSelector {
    DISPLACEMENTMAP_CHANNEL_RED,
    DISPLACEMENTMAP_CHANNEL_GREEN,
    DISPLACEMENTMAP_CHANNEL_BLUE,
    DISPLACEMENTMAP_CHANNEL_ALPHA,
    DISPLACEMENTMAP_CHANNEL_ENDTYPE
};

class SPFeDisplacementMapClass;

class CFeDisplacementMap;

class SPFeDisplacementMap : public SPFilterPrimitive {
public:
	CFeDisplacementMap* cfedisplacementmap;

    int in2; 
    double scale;
    FilterDisplacementMapChannelSelector xChannelSelector;
    FilterDisplacementMapChannelSelector yChannelSelector;
};

struct SPFeDisplacementMapClass {
    SPFilterPrimitiveClass parent_class;
};

class CFeDisplacementMap : public CFilterPrimitive {
public:
	CFeDisplacementMap(SPFeDisplacementMap* map);
	virtual ~CFeDisplacementMap();

	virtual void onBuild(SPDocument* doc, Inkscape::XML::Node* repr);
	virtual void onRelease();

	virtual void onSet(unsigned int key, const gchar* value);

	virtual void onUpdate(SPCtx* ctx, unsigned int flags);

	virtual Inkscape::XML::Node* onWrite(Inkscape::XML::Document* doc, Inkscape::XML::Node* repr, guint flags);

	virtual void onBuildRenderer(Inkscape::Filters::Filter* filter);

private:
	SPFeDisplacementMap* spfedisplacementmap;
};

GType sp_feDisplacementMap_get_type();

#endif /* !SP_FEDISPLACEMENTMAP_H_SEEN */

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
