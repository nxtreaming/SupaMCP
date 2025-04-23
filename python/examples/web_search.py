"""Example of using KMCP with LangChain for web search."""

import asyncio
import os
from dotenv import load_dotenv
from langchain_openai import ChatOpenAI
from kmcp_langchain import KMCPAgent
from kmcp_tools import WebBrowserTool

async def main():
    # Load environment variables
    load_dotenv()
    
    # Create KMCP tools
    browser_tool = WebBrowserTool(server_profile="default")
    
    # Create LLM
    llm = ChatOpenAI(
        model="gpt-4",
        temperature=0.7,
        api_key=os.getenv("OPENAI_API_KEY")
    )
    
    # Create agent
    agent = KMCPAgent(
        llm=llm,
        tools=[browser_tool],
        verbose=True
    )
    
    # Run a web search query
    query = "Find and summarize the latest news about artificial intelligence"
    result = await agent.run(query)
    
    print(f"\nResult: {result}")

if __name__ == "__main__":
    asyncio.run(main())
