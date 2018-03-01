/*
This file is a part of the NVDA project.
URL: http://www.nvda-project.org/
Copyright 2006-2010 NVDA contributers.
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2.0, as published by
    the Free Software Foundation.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
This license can be found at:
http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
*/

#include <string>
#include <sstream>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ia2.h>
#include "nvdaController.h"
#include <common/ia2utils.h>
#include "nvdaHelperRemote.h"

using namespace std;

bool fetchIA2Attributes(IAccessible2* pacc2, map<wstring,wstring>& attribsMap) {
	BSTR attribs=NULL;
	pacc2->get_attributes(&attribs);
	if(!attribs) {
		return false;
	}
	IA2AttribsToMap(attribs,attribsMap);
	SysFreeString(attribs);
	return true;
}

IAccessible2* findAriaAtomic(IAccessible2* pacc2,map<wstring,wstring>& attribsMap) {
	map<wstring,wstring>::iterator i=attribsMap.find(L"atomic");
	bool atomic=(i!=attribsMap.end()&&i->second.compare(L"true")==0);
	IAccessible2* pacc2Atomic=NULL;
	if(atomic) {
		pacc2Atomic=pacc2;
		pacc2Atomic->AddRef();
	} else {
		i=attribsMap.find(L"container-atomic");
		if(i!=attribsMap.end()&&i->second.compare(L"true")==0) {
			IDispatch* pdispParent=NULL;
			pacc2->get_accParent(&pdispParent);
			if(pdispParent) {
				IAccessible2* pacc2Parent=NULL;
				if(pdispParent->QueryInterface(IID_IAccessible2,(void**)&pacc2Parent)==S_OK&&pacc2Parent) {
					map<wstring,wstring> parentAttribsMap;
					if(fetchIA2Attributes(pacc2Parent,parentAttribsMap)) {
						pacc2Atomic=findAriaAtomic(pacc2Parent,parentAttribsMap);
					}
					pacc2Parent->Release();
				}
				pdispParent->Release();
			}
		}
	}
	return pacc2Atomic;
}

bool getTextFromIAccessible(wstring& textBuf, IAccessible2* pacc2, bool useNewText=false, bool recurse=true, bool includeTopLevelText=true) {
	bool gotText=false;
	IAccessibleText* paccText=NULL;
	if(pacc2->QueryInterface(IID_IAccessibleText,(void**)&paccText)!=S_OK) {
		paccText=NULL;
	}
	if(!paccText&&recurse&&!useNewText) {
		//no IAccessibleText interface, so try children instead
		long childCount=0;
		if(!useNewText&&pacc2->get_accChildCount(&childCount)==S_OK&&childCount>0) {
			VARIANT* varChildren=new VARIANT[childCount];
			AccessibleChildren(pacc2,0,childCount,varChildren,&childCount);
			for(int i=0;i<childCount;++i) {
				if(varChildren[i].vt==VT_DISPATCH) {
					IAccessible2* pacc2Child=NULL;
					if(varChildren[i].pdispVal&&varChildren[i].pdispVal->QueryInterface(IID_IAccessible2,(void**)&pacc2Child)==S_OK) {
						map<wstring,wstring> childAttribsMap;
						fetchIA2Attributes(pacc2Child,childAttribsMap);
						auto i=childAttribsMap.find(L"live");
						if(i==childAttribsMap.end()||i->second.compare(L"off")!=0) {
							if(getTextFromIAccessible(textBuf,pacc2Child)) {
								gotText=true;
							}
						}
						pacc2Child->Release();
					}
				}
				VariantClear(varChildren+i);
			}
			delete [] varChildren;
		}
	} else if(paccText) {
		//We can use IAccessibleText because it exists
		BSTR bstrText=NULL;
		long startOffset=0;
		//If requested, get the text from IAccessibleText::newText rather than just IAccessibleText::text.
		if(useNewText) {
			IA2TextSegment newSeg={0};
			if(paccText->get_newText(&newSeg)==S_OK&&newSeg.text) {
				bstrText=newSeg.text;
				startOffset=newSeg.start;
			}
		} else {
			paccText->get_text(0,IA2_TEXT_OFFSET_LENGTH,&bstrText);
		}
		//If we got text, add it to  the string provided, however if there are embedded objects in the text, recurse in to these
		if(bstrText) {
			long textLength=SysStringLen(bstrText);
			IAccessibleHypertext* paccHypertext=NULL;
			if(!recurse||pacc2->QueryInterface(IID_IAccessibleHypertext,(void**)&paccHypertext)!=S_OK) paccHypertext=NULL;
			for(long index=0;index<textLength;++index) {
				wchar_t realChar=bstrText[index];
				bool charAdded=false;
				if(realChar==L'\xfffc') {
					long hyperlinkIndex;
					if(paccHypertext&&paccHypertext->get_hyperlinkIndex(startOffset+index,&hyperlinkIndex)==S_OK) {
						IAccessibleHyperlink* paccHyperlink=NULL;
						if(paccHypertext->get_hyperlink(hyperlinkIndex,&paccHyperlink)==S_OK) {
							IAccessible2* pacc2Child=NULL;
							if(paccHyperlink->QueryInterface(IID_IAccessible2,(void**)&pacc2Child)==S_OK) {
								map<wstring,wstring> childAttribsMap;
								fetchIA2Attributes(pacc2Child,childAttribsMap);
								auto i=childAttribsMap.find(L"live");
								if(i==childAttribsMap.end()||i->second.compare(L"off")!=0) {
									if(getTextFromIAccessible(textBuf,pacc2Child)) {
										gotText=true;
									}
								}
								charAdded=true;
								pacc2Child->Release();
							}
							paccHyperlink->Release();
						}
					}
				}
				if(!charAdded&&includeTopLevelText) {
					textBuf.append(1,realChar);
					charAdded=true;
					if(realChar!=L'\xfffc'&&!iswspace(realChar)) {
						gotText=true;
					}
				}
			}
			if(paccHypertext) paccHypertext->Release();
			SysFreeString(bstrText);
			textBuf.append(1,L' ');
		}
		paccText->Release();
	}
	if(!gotText&&!useNewText) {
		//We got no text from IAccessibleText interface or children, so try name and/or description
		BSTR val=NULL;
		bool valEmpty=true;
		VARIANT varChild;
		varChild.vt=VT_I4;
		varChild.lVal=0;
		pacc2->get_accName(varChild,&val);
		if(val) {
			for(int i=0;val[i]!=L'\0';++i) {
				if(val[i]!=L'\xfffc'&&!iswspace(val[i])) {
					valEmpty=false;
					break;
				}
			}
			if(!valEmpty) {
				gotText=true;
				textBuf.append(val);
				textBuf.append(L" ");
			}
			SysFreeString(val);
			val=NULL;
		}
		valEmpty=true;
		pacc2->get_accDescription(varChild,&val);
		if(val) {
			for(int i=0;val[i]!=L'\0';++i) {
				if(val[i]!=L'\xfffc'&&!iswspace(val[i])) {
					valEmpty=false;
					break;
				}
			}
			if(!valEmpty) {
				gotText=true;
				textBuf.append(val);
			}
			SysFreeString(val);
		}
	}
	return gotText;
}

bool useIAccessibleForLiveRegionUpdate(IAccessible* pacc, VARIANT varChild) {
	//Retrieve the object states, and if its invisible or offscreen ignore the event.
	VARIANT varState, varRole;
	// #7709: Ensure The VARIANT is initialized, as accState may try to clear it before setting it.
	VariantInit(&varState);
	pacc->get_accState(varChild,&varState);
	if(varState.vt==VT_I4&&(varState.lVal&STATE_SYSTEM_INVISIBLE)) {
		VariantClear(&varState);
		return false;
	}
	VariantClear(&varState);
	//Return false for text leaf nodes as their text is available by their parents' IAccessibleText interface.
	//Text leaf nodes have a role of staticText and do not support the IAccessibleHyperlink interface (I.e. they
	//are not represented by an embedded object character on their parent).
	//#8044: Chrome fires redundant events on these nodes which would cause us to double speak live regions if not ignored.
	VariantInit(&varRole);
	pacc->get_accRole(varChild,&varRole);
	if(varRole.vt==VT_I4&&varRole.lVal==ROLE_SYSTEM_STATICTEXT) {
		VariantClear(&varRole);
		IAccessibleHyperlink* paccHyperlink=NULL;
		if(pacc->QueryInterface(IID_IAccessibleHyperlink,(void**)&paccHyperlink)==S_OK) {
			paccHyperlink->Release();
			return true;
		}
		return false;
	}
	VariantClear(&varRole);
	return true;
}

void CALLBACK winEventProcHook(HWINEVENTHOOK hookID, DWORD eventID, HWND hwnd, long objectID, long childID, DWORD threadID, DWORD time) { 
	HWND fgHwnd=GetForegroundWindow();
	//Ignore events for windows that are invisible or are not in the foreground
	if(!IsWindowVisible(hwnd)||(hwnd!=fgHwnd&&!IsChild(fgHwnd,hwnd))) return;
	//Ignore all events but a few types
	switch(eventID) {
		case EVENT_OBJECT_NAMECHANGE:
		case EVENT_OBJECT_DESCRIPTIONCHANGE:
		case EVENT_OBJECT_SHOW:
		case IA2_EVENT_TEXT_UPDATED:
		case IA2_EVENT_TEXT_INSERTED:
		break;
		default:
		return;
	}
	IAccessible* pacc=NULL;
	IServiceProvider* pserv=NULL;
	IAccessible2* pacc2=NULL;
	VARIANT varChild;
	//Try getting the IAccessible from the event
	if(AccessibleObjectFromEvent(hwnd,objectID,childID,&pacc,&varChild)!=S_OK) {
		return;
	}
	if (!useIAccessibleForLiveRegionUpdate(pacc, varChild)) {
		VariantClear(&varChild);
		pacc->Release();
		return;
	}
	VariantClear(&varChild);
	//Retreave an IAccessible2 via IServiceProvider if it exists.
	pacc->QueryInterface(IID_IServiceProvider,(void**)(&pserv));
	pacc->Release();
	if(!pserv) return; 
	pserv->QueryService(IID_IAccessible,IID_IAccessible2,(void**)(&pacc2));
	pserv->Release();
	if(!pacc2) return;
	//Retreave the IAccessible2 attributes, and if the object is not a live region then ignore the event.
	map<wstring,wstring> attribsMap;
	if(!fetchIA2Attributes(pacc2,attribsMap)) {
		pacc2->Release();
		return;
	}
	auto i=attribsMap.find(L"container-live");
	bool live=(i!=attribsMap.end()&&(i->second.compare(L"polite")==0||i->second.compare(L"assertive")==0||i->second.compare(L"rude")==0));
	if(!live) {
		pacc2->Release();
		return;
	}
	i=attribsMap.find(L"container-busy");
	bool busy=(i!=attribsMap.end()&&i->second.compare(L"true")==0);
	if(busy) {
		pacc2->Release();
		return;
	}
	i=attribsMap.find(L"container-relevant");
	bool allowAdditions=false;
	bool allowText=false;
	//If relevant is not specifyed we will default to additions and text, if all is specified then we also use additions and text
	if(i==attribsMap.end()||i->second.compare(L"all")==0) {
		allowText=allowAdditions=true;
	} else { //we support additions if its specified, we support text if its specified
		allowText=(i->second.find(L"text",0)!=wstring::npos);
		allowAdditions=(i->second.find(L"additions",0)!=wstring::npos);
	} 
	// We only support additions or text
	if(!allowAdditions&&!allowText) {
		pacc2->Release();
		return;
	}
	//Only handle show events if additions are allowed
	if(eventID==EVENT_OBJECT_SHOW&&!allowAdditions) {
		pacc2->Release();
		return;
	}
	// If this is a show event and this is not the root of the region and there is a text parent, 
	// We can ignore this event as there will be text events which can handle this better
	if(eventID==EVENT_OBJECT_SHOW) {
		bool ignoreShowEvent=false;
		IDispatch* pdispParent=NULL;
		pacc2->get_accParent(&pdispParent);
		if(pdispParent) {
			// check for text on parent
			IAccessibleText* paccTextParent=NULL;
			if(pdispParent->QueryInterface(IID_IAccessibleText,(void**)&paccTextParent)==S_OK&&paccTextParent) {
				ignoreShowEvent=true;
				paccTextParent->Release();
			}
			if(!ignoreShowEvent) {
				// Check for useful container-live on parent, as if missing or off, then child must be the root 
				// Firstly, we assume we are the root of the region and therefore should ignore the event
				ignoreShowEvent=true;
				IAccessible2* pacc2Parent=NULL;
				if(pdispParent->QueryInterface(IID_IAccessible2,(void**)&pacc2Parent)==S_OK) {
					map<wstring,wstring> parentAttribsMap;
					if(fetchIA2Attributes(pacc2Parent,parentAttribsMap)) {
						i=parentAttribsMap.find(L"container-live");
						if(i!=parentAttribsMap.end()&&(i->second.compare(L"polite")==0||i->second.compare(L"assertive")==0||i->second.compare(L"rude")==0)) {
							// There is a valid container-live that is not off, so therefore the child is definitly not the root
							ignoreShowEvent=false;
						}
					}
					pacc2Parent->Release();
				}
			}
			pdispParent->Release();
		}
		if(ignoreShowEvent) {
			pacc2->Release();
			return;
		}
	}
	// name and description changes can only be announced if relevant is text
	if(!allowText&&(eventID==EVENT_OBJECT_NAMECHANGE||eventID==EVENT_OBJECT_DESCRIPTIONCHANGE)) {
		pacc2->Release();
		return;
	}
	wstring textBuf;
	bool gotText=false;
	IAccessible2* pacc2Atomic=findAriaAtomic(pacc2,attribsMap);
	if(pacc2Atomic) {
		gotText=getTextFromIAccessible(textBuf,pacc2Atomic);
		pacc2Atomic->Release();
	} else if(eventID==EVENT_OBJECT_NAMECHANGE) {
		BSTR name=NULL;
		VARIANT varChild;
		varChild.vt=VT_I4;
		varChild.lVal=0;
		pacc2->get_accName(varChild,&name);
		if(name) {
			textBuf.append(name);
			gotText=true;
			SysFreeString(name);
		}
	} else if(eventID==EVENT_OBJECT_DESCRIPTIONCHANGE) {
		BSTR desc=NULL;
		VARIANT varChild;
		varChild.vt=VT_I4;
		varChild.lVal=0;
		pacc2->get_accDescription(varChild,&desc);
		if(desc) {
			textBuf.append(desc);
			gotText=true;
			SysFreeString(desc);
		}
	} else if(eventID==EVENT_OBJECT_SHOW) {
		gotText=getTextFromIAccessible(textBuf,pacc2);
	} else if(eventID==IA2_EVENT_TEXT_INSERTED||eventID==IA2_EVENT_TEXT_UPDATED) {
		gotText=getTextFromIAccessible(textBuf,pacc2,true,allowAdditions,allowText);
	}
	pacc2->Release();
	if(gotText&&!textBuf.empty()) nvdaController_speakText(textBuf.c_str());
}

void ia2LiveRegions_inProcess_initialize() {
	registerWinEventHook(winEventProcHook);
}

void ia2LiveRegions_inProcess_terminate() {
	unregisterWinEventHook(winEventProcHook);
}
