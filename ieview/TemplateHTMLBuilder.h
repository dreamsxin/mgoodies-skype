class TemplateHTMLBuilder;

#ifndef TEMPLATEHTMLBUILDER_INCLUDED
#define TEMPLATEHTMLBUILDER_INCLUDED

#include "HTMLBuilder.h"

class TemplateHTMLBuilder:public HTMLBuilder
{
protected:
	virtual char *timestampToString(time_t check, int mode);
	bool        isCleared;
	time_t 		startedTime;
	time_t 		getStartedTime();
	const char *groupTemplate;
	bool isDbEventShown(DBEVENTINFO * dbei);
	void buildHeadTemplate(IEView *, IEVIEWEVENT *event);
	void appendEventTemplate(IEView *, IEVIEWEVENT *event);
public:
    TemplateHTMLBuilder();
	void buildHead(IEView *, IEVIEWEVENT *event);
//	void appendEvent(IEView *, IEVIEWEVENT *event);
	void appendEventMem(IEView *, IEVIEWEVENT *event);
};

#endif
