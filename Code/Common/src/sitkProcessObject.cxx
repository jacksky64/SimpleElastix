/*=========================================================================
*
*  Copyright Insight Software Consortium
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*         http://www.apache.org/licenses/LICENSE-2.0.txt
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
*
*=========================================================================*/
#include "sitkProcessObject.h"
#include "sitkCommand.h"

#include "itkProcessObject.h"
#include "itkCommand.h"

#include <iostream>
#include <algorithm>

#include "nsstd/functional.h"

namespace itk {
namespace simple {

namespace
{
static bool GlobalDefaultDebug = false;

static itk::AnyEvent eventAnyEvent;
static itk::AbortEvent eventAbortEvent;
static itk::DeleteEvent eventDeleteEvent;
static itk::EndEvent eventEndEvent;;
static itk::IterationEvent eventIterationEvent;
static itk::ProgressEvent eventProgressEvent;
static itk::StartEvent eventStartEvent;
static itk::UserEvent eventUserEvent;


// Create an ITK EventObject from the SimpleITK enumerated type.
const itk::EventObject &GetITKEventObject(EventEnum e)
{
  switch (e)
    {
    case sitkAnyEvent:
      return eventAnyEvent;
    case sitkAbortEvent:
      return eventAbortEvent;
    case sitkDeleteEvent:
      return eventDeleteEvent;
    case sitkEndEvent:
      return eventEndEvent;
    case sitkIterationEvent:
      return eventIterationEvent;
    case sitkProgressEvent:
      return eventProgressEvent;
    case sitkStartEvent:
      return eventStartEvent;
    case sitkUserEvent:
      return eventUserEvent;
    default:
      sitkExceptionMacro("LogicError: Unexpected event case!");
    }
}

// Local class to adapt a sitk::Command to ITK's command.
// It utilizes a raw pointer, and relies on the sitk
// ProcessObject<->Command reference to automatically remove it.
class SimpleAdaptorCommand
  : public itk::Command
{
public:

  typedef SimpleAdaptorCommand Self;
  typedef SmartPointer< Self >  Pointer;

  itkNewMacro(Self);

  itkTypeMacro(SimpleAdaptorCommand, Command);

  void SetSimpleCommand( itk::simple::Command *cmd )
    {
      m_That=cmd;
    }

  /**  Invoke the member function. */
  virtual void Execute(Object *, const EventObject & )
  {
    if (m_That)
      {
      m_That->Execute();
      }
  }

  /**  Invoke the member function with a const object */
  virtual void Execute(const Object *, const EventObject & )
  {
    if ( m_That )
      {
      m_That->Execute();
      }
  }

protected:
  itk::simple::Command *                    m_That;
  SimpleAdaptorCommand():m_That(0) {}
  virtual ~SimpleAdaptorCommand() {}

private:
  SimpleAdaptorCommand(const Self &); //purposely not implemented
  void operator=(const Self &);        //purposely not implemented
};

} // end anonymous namespace

//----------------------------------------------------------------------------

//
// Default constructor that initializes parameters
//
ProcessObject::ProcessObject ()
  : m_Debug(ProcessObject::GetGlobalDefaultDebug()),
    m_NumberOfThreads(ProcessObject::GetGlobalDefaultNumberOfThreads()),
    m_ActiveProcess(NULL),
    m_ProgressMeasurement(0.0)
{
}


//
// Destructor
//
ProcessObject::~ProcessObject ()
{
  // ensure to remove reference between sitk commands and process object
  Self::RemoveAllCommands();
}


std::ostream & ProcessObject::ToStringHelper(std::ostream &os, const char &v)
{
  os << int(v);
  return os;
}


std::ostream & ProcessObject::ToStringHelper(std::ostream &os, const signed char &v)
{
  os << int(v);
  return os;
}


std::ostream & ProcessObject::ToStringHelper(std::ostream &os, const unsigned char &v)
{
  os << int(v);
  return os;
}


void ProcessObject::DebugOn()
{
  this->m_Debug = true;
}


void ProcessObject::DebugOff()
{
  this->m_Debug = false;
}


bool ProcessObject::GetDebug() const
{
  return this->m_Debug;
}


void ProcessObject::SetDebug(bool debugFlag)
{
  this->m_Debug = debugFlag;
}


void ProcessObject::GlobalDefaultDebugOn()
{
  GlobalDefaultDebug = true;
}


void ProcessObject::GlobalDefaultDebugOff()

{
  GlobalDefaultDebug = false;
}


bool ProcessObject::GetGlobalDefaultDebug()
{
  return GlobalDefaultDebug;
}


void ProcessObject::SetGlobalDefaultDebug(bool debugFlag)
{
  GlobalDefaultDebug = debugFlag;
}


void ProcessObject::GlobalWarningDisplayOn()
{
  itk::Object::GlobalWarningDisplayOn();
}


void ProcessObject::GlobalWarningDisplayOff()
{
  itk::Object::GlobalWarningDisplayOff();
}


bool ProcessObject::GetGlobalWarningDisplay()
{
  return itk::Object::GetGlobalWarningDisplay();
}


void ProcessObject::SetGlobalWarningDisplay(bool flag)
{
  itk::Object::SetGlobalWarningDisplay(flag);
}


void ProcessObject::SetGlobalDefaultNumberOfThreads(unsigned int n)
{
  MultiThreader::SetGlobalDefaultNumberOfThreads(n);
}


unsigned int ProcessObject::GetGlobalDefaultNumberOfThreads()
{
  return MultiThreader::GetGlobalDefaultNumberOfThreads();
}


void ProcessObject::SetNumberOfThreads(unsigned int n)
{
  m_NumberOfThreads = n;
}


unsigned int ProcessObject::GetNumberOfThreads() const
{
  return m_NumberOfThreads;
}


int ProcessObject::AddCommand(EventEnum event, Command &cmd)
{
  // add to our list of event, command pairs
  m_Commands.push_back(EventCommand(event,&cmd));

  // register ourselves with the command
  cmd.AddProcessObject(this);

  if (this->m_ActiveProcess)
    {
    const itk::EventObject &itkEvent = GetITKEventObject(event);

    // adapt sitk command to itk command
    SimpleAdaptorCommand::Pointer itkCommand = SimpleAdaptorCommand::New();
    itkCommand->SetSimpleCommand(&cmd);
    itkCommand->SetObjectName(cmd.GetName()+" "+itkEvent.GetEventName());

    m_Commands.back().m_ITKTag = this->PreUpdateAddObserver(this->m_ActiveProcess, itkEvent, itkCommand);
    }

  return 0;
}


void ProcessObject::RemoveAllCommands()
{
  // set's the m_Commands to an empty list via a swap
  std::list<EventCommand> oldCommands;
  swap(oldCommands, m_Commands);

  // we must only call RemoveProcessObject once for each command
  // so make a unique list of the Commands.
  oldCommands.sort();
  oldCommands.unique();

  std::list<EventCommand>::iterator i = oldCommands.begin();
  while( i != oldCommands.end() )
    {
    i++->m_Command->RemoveProcessObject(this);
    }
}


bool ProcessObject::HasCommand( EventEnum event ) const
{
  std::list<EventCommand>::const_iterator i = m_Commands.begin();
  while( i != m_Commands.end() )
    {
    if (i->m_Event == event)
      {
      return true;
      }
    ++i;
    }
  return false;
}


float ProcessObject::GetProgress( ) const
{
  if ( this->m_ActiveProcess )
    {
    return this->m_ActiveProcess->GetProgress();
    }
  return m_ProgressMeasurement;
}


void ProcessObject::Abort()
{
  if ( this->m_ActiveProcess )
    {
    this->m_ActiveProcess->AbortGenerateDataOn();
    }
}


void ProcessObject::PreUpdate(itk::ProcessObject *p)
{
  assert(p);

  // propagate number of threads
  p->SetNumberOfThreads(this->GetNumberOfThreads());

  try
    {
    this->m_ActiveProcess = p;

    // register commands
    for (std::list<EventCommand>::iterator i = m_Commands.begin();
         i != m_Commands.end();
         ++i)
      {
      if (i->m_ITKTag != std::numeric_limits<unsigned long>::max())
        {
        sitkExceptionMacro("Commands already registered to another process object!");
        }
      const itk::EventObject &itkEvent = GetITKEventObject(i->m_Event);

      Command *cmd = i->m_Command;

      // adapt sitk command to itk command
      SimpleAdaptorCommand::Pointer itkCommand = SimpleAdaptorCommand::New();
      itkCommand->SetSimpleCommand(cmd);
      itkCommand->SetObjectName(cmd->GetName()+" "+itkEvent.GetEventName());

      // allow derived classes to customize when observer is added.
      i->m_ITKTag = this->PreUpdateAddObserver(p, itkEvent, itkCommand);
      }

    // add command on active process deletion
    itk::SimpleMemberCommand<Self>::Pointer onDelete = itk::SimpleMemberCommand<Self>::New();
    onDelete->SetCallbackFunction(this, &Self::OnActiveProcessDelete);
    p->AddObserver(itk::DeleteEvent(), onDelete);

    }
  catch (...)
    {
    this->m_ActiveProcess = NULL;
    throw;
    }



  if (this->GetDebug())
     {
     std::cout << "Executing ITK filter:" << std::endl;
     p->Print(std::cout);
     }

}


unsigned long ProcessObject::PreUpdateAddObserver( itk::ProcessObject *p,
                                          const itk::EventObject &e,
                                          itk::Command *c)
{
  return p->AddObserver(e,c);
}

itk::ProcessObject *ProcessObject::GetActiveProcess( )
{
  if (this->m_ActiveProcess)
    {
    return this->m_ActiveProcess;
    }
  sitkExceptionMacro("No active process for \"" << this->GetName() << "\"!");
}


void ProcessObject::OnActiveProcessDelete( )
{
  if (this->m_ActiveProcess)
    {
    this->m_ProgressMeasurement = this->m_ActiveProcess->GetProgress();
    }
  else
    {
    this->m_ProgressMeasurement = 0.0f;
    }

  // clear registered command IDs
  for (std::list<EventCommand>::iterator i = m_Commands.begin();
         i != m_Commands.end();
         ++i)
      {
      i->m_ITKTag = std::numeric_limits<unsigned long>::max();
      }

  this->m_ActiveProcess = NULL;
}


void ProcessObject::onCommandDelete(const itk::simple::Command *cmd) throw()
{
  // remove command from m_Command book keeping list, and remove it
  // from the  ITK ProcessObject
  std::list<EventCommand>::iterator i =  this->m_Commands.begin();
  while ( i !=  this->m_Commands.end() )
    {
    if ( cmd == i->m_Command )
      {
      if (i->m_ITKTag != std::numeric_limits<unsigned long>::max()
          && this->m_ActiveProcess)
        {
        this->m_ActiveProcess->RemoveObserver(i->m_ITKTag);
        }
       this->m_Commands.erase(i++);
      }
    else
      {
      ++i;
      }

    }
}

} // end namespace simple
} // end namespace itk
