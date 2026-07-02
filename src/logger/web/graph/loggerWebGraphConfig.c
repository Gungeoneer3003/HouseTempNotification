//Statement of Purpose:
/*
 * This file contains the implementation for configuring logger web graphs.
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "../loggerWeb.h"
#include "../loggerWebInternal.h"
#include <stdlib.h>
#include <string.h>

//Function prototypes for internal functions used in the logger web server
static void freeGraph(LoggerWebGraph* graph);
static LoggerWebGraph* findGraphByTitle(LoggerWebServer* server, const char* title);
static int appendGraphVert(LoggerWebGraph* graph,
                           const char* column,
                           size_t column_index,
                           const char* value,
                           const char* color);
static int appendGraphSpan(LoggerWebGraph* graph,
                           const char* column,
                           size_t column_index,
                           const char* start_value,
                           const char* end_value,
                           const char* color);

//Insert a graph with the specified title, x-axis column, 
//and only one y-axis column into the logger web server
int loggerWebInsertGraph(const char* title,
                         const char* x_column,
                         const char* y_column) {
    const char* y_columns[] = {y_column};
    return loggerWebInsertGraphSeries(title, x_column, y_columns, 1);
}

//Insert a graph with the specified title, x-axis column, alongside
//allowing for multiple y-axis columns into the logger web server
int loggerWebInsertGraphSeries(const char* title,
                               const char* x_column,
                               const char* const* y_columns,
                               size_t y_column_count) {
    
    //Validate the input parameters
    if (!title || !*title || !x_column || !*x_column ||
        !y_columns || y_column_count == 0) {
        return 0;
    }

    //Lock the active server mutex to ensure thread safety
    loggerWebMutexLock(&active_server_mutex);
    LoggerWebServer* server = active_server;
    if (!server) {
        loggerWebMutexUnlock(&active_server_mutex);
        return 0;
    }

    //Resolve the index of the x-axis column in the server's column headers
    size_t x_index = 0;
    if (!loggerWebResolveColumnIndex(server, x_column, &x_index)) {
        loggerWebMutexUnlock(&active_server_mutex);
        return 0;
    }

    //Check if a graph with the same title already exists in the server
    LoggerWebGraph graph;
    memset(&graph, 0, sizeof(graph));
    graph.title = loggerWebCopyString(title);
    graph.x_column = loggerWebCopyString(x_column);
    graph.x_index = x_index;
    graph.series_count = y_column_count;
    graph.series = calloc(y_column_count, sizeof(*graph.series));

    if (!graph.title || !graph.x_column || !graph.series) {
        freeGraph(&graph);
        loggerWebMutexUnlock(&active_server_mutex);
        return 0;
    }

    //Resolve the indices of the y-axis columns in the server's column headers
    for (size_t i = 0; i < y_column_count; i++) {
        if (!y_columns[i] || !*y_columns[i] ||
            !loggerWebResolveColumnIndex(server, y_columns[i], &graph.series[i].index)) {
            freeGraph(&graph);
            loggerWebMutexUnlock(&active_server_mutex);
            return 0;
        }

        graph.series[i].name = loggerWebCopyString(y_columns[i]);
        if (!graph.series[i].name) {
            freeGraph(&graph);
            loggerWebMutexUnlock(&active_server_mutex);
            return 0;
        }
    }

    //Check if the server has enough capacity to accommodate the new graph
    if (server->graph_count == server->graph_capacity) {
        size_t next_capacity = server->graph_capacity == 0 ? 4 : server->graph_capacity * 2;
        LoggerWebGraph* next_graphs = realloc(server->graphs,
                                              next_capacity * sizeof(*server->graphs));
        if (!next_graphs) {
            freeGraph(&graph);
            loggerWebMutexUnlock(&active_server_mutex);
            return 0;
        }

        server->graphs = next_graphs;
        server->graph_capacity = next_capacity;
    }

    //Add the new graph to the server's graph array and increment the graph count
    server->graphs[server->graph_count] = graph;
    server->graph_count++;

    loggerWebMutexUnlock(&active_server_mutex);
    return 1;
}

//Set whether to show statistics on the graphs page
int loggerWebShowStats(int enabled) {
    loggerWebMutexLock(&active_server_mutex);
    LoggerWebServer* server = active_server;
    if (!server) {
        loggerWebMutexUnlock(&active_server_mutex);
        return 0;
    }

    server->show_stats = enabled != 0;
    loggerWebMutexUnlock(&active_server_mutex);
    return 1;
}

//Set whether to show the vertical lines for events on the graphs page
int loggerWebShowVerts(const char* graph_title,
                       const char* column,
                       const char* value,
                       const char* color) {
    //Validate the input parameters
    if (!graph_title || !*graph_title || !column || !*column || !value || !*value) {
        return 0;
    }

    //Lock the active server mutex to ensure thread safety
    loggerWebMutexLock(&active_server_mutex);
    LoggerWebServer* server = active_server;
    if (!server) {
        loggerWebMutexUnlock(&active_server_mutex);
        return 0;
    }

    //Find the graph by title and resolve the column index for the specified column
    LoggerWebGraph* graph = findGraphByTitle(server, graph_title);
    size_t column_index = 0;
    if (!graph || !loggerWebResolveColumnIndex(server, column, &column_index)) {
        loggerWebMutexUnlock(&active_server_mutex);
        return 0;
    }

    //Append the vertical line to the graph with the specified parameters
    int ok = appendGraphVert(graph,
                             column,
                             column_index,
                             value,
                             color && *color ? color : "#ef4444");

    loggerWebMutexUnlock(&active_server_mutex);
    return ok;
}

//Set whether to show a span (highlighted range) on the graphs page
int loggerWebShowSpan(const char* graph_title,
                      const char* column,
                      const char* start_value,
                      const char* end_value,
                      const char* color) {
    //Validate the input parameters
    if (!graph_title || !*graph_title || !column || !*column ||
        !start_value || !*start_value || !end_value || !*end_value) {
        return 0;
    }

    //Lock the active server mutex to ensure thread safety
    loggerWebMutexLock(&active_server_mutex);
    LoggerWebServer* server = active_server;
    if (!server) {
        loggerWebMutexUnlock(&active_server_mutex);
        return 0;
    }

    //Find the graph by title and resolve the column index for the specified column
    LoggerWebGraph* graph = findGraphByTitle(server, graph_title);
    size_t column_index = 0;
    if (!graph || !loggerWebResolveColumnIndex(server, column, &column_index)) {
        loggerWebMutexUnlock(&active_server_mutex);
        return 0;
    }

    //Append the span to the graph with the specified parameters
    int ok = appendGraphSpan(graph,
                             column,
                             column_index,
                             start_value,
                             end_value,
                             color && *color ? color : "#f59e0b");
    
    loggerWebMutexUnlock(&active_server_mutex);
    return ok;
}

//Free the memory allocated for the graphs in the logger web server
void loggerWebFreeGraphs(LoggerWebServer* server) {
    //Check if the server and its graphs are valid
    if (!server || !server->graphs) {
        return;
    }

    //Free each graph in the server's graph array
    for (size_t i = 0; i < server->graph_count; i++) {
        freeGraph(&server->graphs[i]);
    }

    //Free the graph array and reset the graph count and capacity
    free(server->graphs);
    server->graphs = NULL;
    server->graph_count = 0;
    server->graph_capacity = 0;
}

//Check if the logger web server has any graphs configured
int loggerWebHasGraphs(const LoggerWebServer* server) {
    int has_graphs = 0;

    loggerWebMutexLock(&active_server_mutex);
    has_graphs = server && server->graph_count > 0;
    loggerWebMutexUnlock(&active_server_mutex);

    return has_graphs;
}

//Free the memory allocated for a single graph
//This includes its title, x-axis column, series, vertical lines, and spans
static void freeGraph(LoggerWebGraph* graph) {
    //Check if the graph is valid
    if (!graph) {
        return;
    }

    //Free the title and x-axis column strings
    free(graph->title);
    free(graph->x_column);

    //Free the series array and its name strings
    if (graph->series) {
        for (size_t i = 0; i < graph->series_count; i++) {
            free(graph->series[i].name);
        }
    }

    free(graph->series);

    //Free the vertical lines array and its strings
    if (graph->verts) {
        for (size_t i = 0; i < graph->vert_count; i++) {
            free(graph->verts[i].column);
            free(graph->verts[i].value);
            free(graph->verts[i].color);
        }
    }

    //Free the vertical lines array
    free(graph->verts);

    //Free the spans array and its strings
    if (graph->spans) {
        for (size_t i = 0; i < graph->span_count; i++) {
            free(graph->spans[i].column);
            free(graph->spans[i].start_value);
            free(graph->spans[i].end_value);
            free(graph->spans[i].color);
        }
    }

    free(graph->spans);
    
    //Conclude by resetting the graph structure to zero to avoid dangling pointers
    memset(graph, 0, sizeof(*graph));
}

//Find a graph by its title in the logger web server
static LoggerWebGraph* findGraphByTitle(LoggerWebServer* server, const char* title) {
    if (!server || !title) {
        return NULL;
    }

    for (size_t i = 0; i < server->graph_count; i++) {
        if (loggerWebStringEqualsIgnoreCase(server->graphs[i].title, title)) {
            return &server->graphs[i];
        }
    }

    return NULL;
}

//Append a vertical line to the specified graph
//Given column, value, and color
static int appendGraphVert(LoggerWebGraph* graph,
                           const char* column,
                           size_t column_index,
                           const char* value,
                           const char* color) {
    //Check if the graph has enough capacity to accommodate the new vertical line
    if (graph->vert_count == graph->vert_capacity) {
        size_t next_capacity = graph->vert_capacity == 0 ? 2 : graph->vert_capacity * 2;
        LoggerWebVert* next_verts = realloc(graph->verts, next_capacity * sizeof(*graph->verts));
        if (!next_verts) {
            return 0;
        }

        graph->verts = next_verts;
        graph->vert_capacity = next_capacity;
    }

    //Allocate and initialize the new vertical line in the graph's verts array
    LoggerWebVert* vert = &graph->verts[graph->vert_count];
    memset(vert, 0, sizeof(*vert));
    vert->column = loggerWebCopyString(column);
    vert->value = loggerWebCopyString(value);
    vert->color = loggerWebCopyString(color);
    vert->column_index = column_index;

    //Check if any of the allocations for the vertical line failed
    if (!vert->column || !vert->value || !vert->color) {
        free(vert->column);
        free(vert->value);
        free(vert->color);
        memset(vert, 0, sizeof(*vert));
        return 0;
    }

    //Increment the count of vertical lines in the graph
    graph->vert_count++;
    return 1;
}

//Append a span (highlighted range) to the specified graph
//Given column, start value, end value, and color
static int appendGraphSpan(LoggerWebGraph* graph,
                           const char* column,
                           size_t column_index,
                           const char* start_value,
                           const char* end_value,
                           const char* color) {
    //Check if the graph has enough capacity to accommodate the new span
    if (graph->span_count == graph->span_capacity) {
        size_t next_capacity = graph->span_capacity == 0 ? 2 : graph->span_capacity * 2;
        LoggerWebSpan* next_spans = realloc(graph->spans,
                                            next_capacity * sizeof(*graph->spans));
        if (!next_spans) {
            return 0;
        }

        graph->spans = next_spans;
        graph->span_capacity = next_capacity;
    }

    //Allocate and initialize the new span in the graph's spans array
    LoggerWebSpan* span = &graph->spans[graph->span_count];
    memset(span, 0, sizeof(*span));
    span->column = loggerWebCopyString(column);
    span->start_value = loggerWebCopyString(start_value);
    span->end_value = loggerWebCopyString(end_value);
    span->color = loggerWebCopyString(color);
    span->column_index = column_index;

    //Check if any of the allocations for the span failed
    if (!span->column || !span->start_value || !span->end_value || !span->color) {
        free(span->column);
        free(span->start_value);
        free(span->end_value);
        free(span->color);
        memset(span, 0, sizeof(*span));
        return 0;
    }

    //Increment the count of spans in the graph
    graph->span_count++;
    return 1;
}

