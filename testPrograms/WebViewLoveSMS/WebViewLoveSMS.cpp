/*
Copyright (C) 2011 MoSync AB

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License,
version 2, as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
MA 02110-1301, USA.
*/

/**
 * @file WebViewLoveSMS.cpp
 * @author Mikael Kindborg
 *
 * Application for sending Love SMSs.
 *
 * This program illustrates how to use WebView for the
 * user interface of a MoSync C++ application.
 *
 * An application can divide the program code between the
 * WebView layer and the C++ layer in a variety of ways.
 * In one extreme, the WebView is used purely for
 * rendering the user interface, the HTML/CSS/JavaScript
 * code could even be generated by C++ code. In the other
 * extreme, almost the entire application is written in
 * HTML/CSS/JavaScript, and only the calls needed to access
 * native functionality via the MoSync API is written in C++.
 *
 * Which approach is chosen depends on the preferences of the
 * development team, existing code and libraries, compatibility
 * considerations etc. Some teams may prefer to be C++ centric,
 * white others may prefer to do most of the development using
 * JavScript and web technologies.
 */

#include <ma.h>
#include <maheap.h>
#include <mastring.h>
#include <mavsprintf.h>
#include <MAUtil/String.h>
#include <IX_WIDGET.h>
#include <conprint.h>
#include "MAHeaders.h"
#include "WebViewUtil.h"

using namespace MoSync;

class WebViewLoveSMSApp
{
private:
	MAWidgetHandle mScreen;
	MAWidgetHandle mWebView;
	Platform* mPlatform;
	MAUtil::String mLoveMessage;
	MAUtil::String mKissMessage;

public:
	WebViewLoveSMSApp()
	{
		createUI();
		createMessageStrings();
	}

	virtual ~WebViewLoveSMSApp()
	{
		destroyUI();
	}

	void createMessageStrings()
	{
		// Create a message string with lots of
		// heart smileys!
		mLoveMessage = "";
		while (mLoveMessage.length() < 150)
		{
			mLoveMessage += "<3 ";
		}

		// Create a message string with lots of
		// kiss smileys!
		mKissMessage = "";
		while (mKissMessage.length() < 150)
		{
			mKissMessage += ":-* ";
		}
	}

	void createUI()
	{
		mPlatform = Platform::create();

		MAUtil::String html =
			mPlatform->createTextFromHandle(MainPage_html);

		// Create screen.
		mScreen = maWidgetCreate(MAW_SCREEN);
		widgetShouldBeValid(mScreen, "Could not create screen");

		// Create web view.
		mWebView = createWebView(html);

		// Compose objects.
		maWidgetAddChild(mScreen, mWebView);

		// Show the screen.
		maWidgetScreenShow(mScreen);
	}

	MAWidgetHandle createWebView(const MAUtil::String& html)
	{
		// Create web view
		MAWidgetHandle webView = maWidgetCreate(MAW_WEB_VIEW);
		widgetShouldBeValid(webView, "Could not create web view");

		// Set size of vew view to fill the parent.
		maWidgetSetProperty(webView, "width", "-1");
		maWidgetSetProperty(webView, "height", "-1");

		// Enable zooming.
		maWidgetSetProperty(webView, "enableZoom", "true");

		// Set the HTML the web view displays.
		maWidgetSetProperty(webView, "html", html.c_str());

		WebViewMessage::getMessagesFor(webView);

		return webView;
	}

	void destroyUI()
	{
		maWidgetDestroy(mScreen);
		delete mPlatform;
	}

	void runEventLoop()
	{
		MAEvent event;

		bool isRunning = true;
		while (isRunning)
		{
			maWait(0);
			maGetEvent(&event);
			switch (event.type)
			{
				case EVENT_TYPE_CLOSE:
					isRunning = false;
					break;

				case EVENT_TYPE_KEY_PRESSED:
					if (event.key == MAK_MENU)
					{
						// TODO: Show menu in HTML/JS.
					}
					if (event.key == MAK_BACK)
					{
						isRunning = false;
					}
					break;

				case EVENT_TYPE_WIDGET:
					handleWidgetEvent((MAWidgetEventData*) event.data);
					break;
			}
		}
	}

	void handleWidgetEvent(MAWidgetEventData* widgetEvent)
	{
		// Handle messages from the WebView widget.
		if (MAW_EVENT_WEB_VIEW_HOOK_INVOKED == widgetEvent->eventType &&
			MAW_CONSTANT_HARD == widgetEvent->hookType)
		{
			// Get message.
			WebViewMessage message(widgetEvent->urlData);

			if (message.is("SendLoveSMS"))
			{
				// Save phone no and send SMS.
				savePhoneNoAndSendSMS(
					message.getParam(0),
					mLoveMessage);
			}

			if (message.is("SendKissSMS"))
			{
				// Save phone no and send SMS.
				savePhoneNoAndSendSMS(
					message.getParam(0),
					mKissMessage);
			}

			if (message.is("PageLoaded"))
			{
				// Set saved phone number here.
				setSavedPhoneNo();
			}
		}
	}

	void savePhoneNoAndSendSMS(
		const MAUtil::String& phoneNo,
		const MAUtil::String&  message)
	{
		lprintfln("*** SMS to: %s", phoneNo.c_str());
		lprintfln("*** SMS data: %s", message.c_str());

		// Save the phone number.
		savePhoneNo(phoneNo);

		// Send the message.
		int result = maSendTextSMS(
			phoneNo.c_str(),
			message.c_str());

		// Provide feedback via JS.
		if (0 == result)
		{
			displayMessage("Lovely SMS sent :)");
		}
		else
		{
			displayMessage("Could not send SMS :(");
		}
	}

	/**
	 * Display a status message in the WebView.
	 */
	void displayMessage(const MAUtil::String& message)
	{
		char script[512];
		sprintf(
			script,
			"javascript:DisplayMessage('%s')",
			message.c_str());
		maWidgetSetProperty(mWebView, "url", script);
	}

	/**
	 * Read saved phone number and set it on
	 * the JavaScript side.
	 */
	void setSavedPhoneNo()
	{
		char script[512];
		sprintf(
			script,
			"javascript:SetPhoneNo('%s')",
			loadPhoneNo().c_str());
		maWidgetSetProperty(mWebView, "url", script);
	}

	/**
	 * Save the phone number.
	 */
	void savePhoneNo(const MAUtil::String& phoneNo)
	{
		MAUtil::String path =
			mPlatform->getLocalPath() +
			"SavedPhoneNo";
		mPlatform->writeTextToFile(path, phoneNo);
	}

	/**
	 * Load the phone number.
	 */
	MAUtil::String loadPhoneNo()
	{
		MAUtil::String path =
			mPlatform->getLocalPath() +
			"SavedPhoneNo";
		MAUtil::String phoneNo;
		bool success = mPlatform->readTextFromFile(path, phoneNo);
		if (success)
		{
			return phoneNo;
		}
		else
		{
			return "";
		}
	}

	void widgetShouldBeValid(MAWidgetHandle widget, const char* panicMessage)
	{
		if (widget <= 0)
		{
			maPanic(0, panicMessage);
		}
	}
};
// End of class WebViewLoveSMSApp

/**
 * Main function that is called when the program starts.
 */
extern "C" int MAMain()
{
	WebViewLoveSMSApp app;
	app.runEventLoop();
	return 0;
}
